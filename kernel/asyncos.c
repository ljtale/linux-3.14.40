/* asyncos.c inter-syscall parallelism kernel support, asynchronous syscall execution
 **/

#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <linux/kthread.h>
#include <linux/spinlock.h>
#include <asm/errno.h>
#include <asm/barrier.h>
#include <linux/file.h>
#include <linux/fs_struct.h>



#include "asyncos.h"

#define QUEUE_SIZE	8
#define NUM_CORE	4

#define TEST_READ

struct syscall_queue{
	struct syscall_entry sq[QUEUE_SIZE] __attribute__ ((aligned (CACHE_LINE_SIZE_X86_64)));
};

struct syscall_queue sys_queue[NUM_CORE] __attribute__ ((aligned (CACHE_LINE_SIZE_X86_64)));

enum queue_entry_status{
	SC_FREE,
	SC_ALLOCATED,
	SC_POSTED,
	SC_COMPLETE,
	SC_ACQUIRED
};


static struct task_struct *kt[NUM_CORE];
static uchar init_flag = 0;
static int thread_data1 = 0;
static int thread_data2 = 0;

static struct files_struct *files[NUM_CORE];
static struct fs_struct *fs[NUM_CORE];


/* worker thread to take over specific syscall queue*/
static inline int 
worker(void *data){
	int retval = -EPERM;
	int q_index = *((int *)data);
	struct syscall_entry *e;
	ssize_t sys_ret = -1;
	struct files_struct *files_temp;
	struct fs_struct *fs_temp;
	smp_rmb();
	trace_printk("jie: worker->kernel thread for %d-th syscall queue\n", q_index);
	/* the following code will work on sys_queue[q_index];*/
	for(;;){
		int q;
		/* spin on the corresponding syscall queue to check the pending syscalls*/
		do{
			for(q = 0; q < QUEUE_SIZE; q++){
				if(sys_queue[q_index].sq[q].status == SC_POSTED){
					if(cas(SC_POSTED, SC_ACQUIRED,&sys_queue[q_index].sq[q].status)){
						e = &sys_queue[q_index].sq[q];
						smp_wmb();
						goto exec_syscall;
					}
				}
				smp_rmb();
			}
		}while(1);		
exec_syscall:
		/* FIXME: at this point, I only test read, so I know there are three arguments
		 * but for generic syscalls, I need a way to know about the number of arguments*/	
#ifdef TEST_READ
		trace_printk("jie: worker->arg1 %d, arg2 0x%lx, arg3 %lu, pid %d\n",
		(int)e->arg1, (unsigned long)e->arg2, (size_t)e->arg3, (int)sys_getpid());
		trace_printk(KERN_INFO "jie: worker->files 0x%lx, fs 0x%lx\n",
		(unsigned long)current->files, (unsigned long)current->fs);

		files_temp = current->files;
		fs_temp = current->fs;
	
		current->files = files[q_index];
	//	current->fs = fs[q_index];
	
		smp_wmb();
		trace_printk(KERN_INFO "jie: worker->files 0x%lx, fs 0x%lx\n",
		(unsigned long)kt[q_index]->files, (unsigned long)kt[q_index]->fs);
		sys_ret = sys_read((unsigned int)e->arg1, (char __user *)e->arg2, (size_t)e->arg3);
		trace_printk("jie: worker-> sys_read returns %d\n", (int)sys_ret);
		smp_wmb();
		e->status = sys_ret >=0 ? SC_COMPLETE : SC_FREE;
		smp_wmb();

#else
		/* FIXME: the following code is not working at present*/
/*
		trace_printk("jie: worker->syscall num %d, syscall arg cnt %d\n", e->sys_num, e->arg_cnt);
		func = (void *)*(sys_call_table + e->sys_num);
		if(e->arg_cnt == 0){
			func_zero = *(sys_call_table + e->sys_num);
		}
		switch(e->arg_cnt){
			case 0:
				sys_ret = func_zero();
				break;
			case 1:
				sys_ret = func(e->arg1);
				break;
			case 2:
				sys_ret = func(e->arg1, e->arg2);
				break;
			case 3:
				sys_ret = func(e->arg1, e->arg2, e->arg3);
				break;
			case 4:
				sys_ret = func(e->arg1, e->arg2, e->arg3, e->arg4);
				break;
			case 5:
				sys_ret = func(e->arg1, e->arg2, e->arg3, e->arg4, e->arg5);
				break;
			case 6:
				sys_ret = func(e->arg1, e->arg2, e->arg3, e->arg4, e->arg5, e->arg6);
				break;
			default:
				break;
		}
*/
#endif	
		e->arg1 = (int64_t)sys_ret;
		smp_wmb();
		current->files = files_temp;
//		current->fs = fs_temp;
		smp_wmb();

	}/*for loop*/
	/* is supposed not to be here*/
	return retval;
}


static inline int 
__get_free_entry(int q_index){
	unsigned long i;
	do{
		for( i = 2; i < QUEUE_SIZE; i++ ){
			if(cas(SC_FREE, SC_ALLOCATED, &sys_queue[q_index].sq[i].status)){
				trace_printk("jie: get_free_entry-> get entry %lu\n",i);
				smp_mb();
				return i;
			} 
		}
		relax();
	}while(1);
	/* will block until there is one available slot*/
	return -1;
}

static inline int64_t 
__complete(int q_index, int pos){
	int64_t retval = -1;
	struct syscall_entry *e = &sys_queue[q_index].sq[pos];
	int cnt = 0;
	do{
		if(e->status == SC_COMPLETE){
			retval = (int64_t)e->arg1;
			trace_printk("jie: __complete-> complete pos %d in queue %d, retval %lld\n", pos, q_index, retval);
			if(cas(SC_COMPLETE, SC_FREE, &e->status)){
				smp_mb();
				return retval;
			}
		}
		/* dummy code here*/
		if(cnt > 10){
			break;
		}
		cnt++;
	}while(1);
	/* FIXME: should add some time out here to avoid deadlocking*/
	/* should never reach here*/
	return retval;
}

SYSCALL_DEFINE0(asyncos_init){
	size_t retval = -EPERM;
	unsigned long i;

	thread_data1 = 0;
	thread_data2 = 1;
	/* initialize the syscall queue*/	
	for(i = 0; i < QUEUE_SIZE; i++ ){
		/* I'm not going to loop through all the sys queues*/
		sys_queue[0].sq[i].status = SC_FREE;
		sys_queue[1].sq[i].status = SC_FREE;
	}
	smp_wmb();
	trace_printk("jie: init-> asyncos_init flag %d\n", init_flag);
	if(init_flag == 1){
		trace_printk("jie: init->asyncos_init is already called\n");
		return retval;
	}
	trace_printk(KERN_INFO "jie: init->asyncos_init !\n");
	trace_printk(KERN_INFO "jie: init->system call table address: 0x%lx\n",(unsigned long)(sys_call_table));
	/* create two worker threads, once create, they cannot be terminated*/
	BUG_ON((thread_data1 > 3 || thread_data2 > 3));
	kt[0] = kthread_create(worker, (void *)&thread_data1, "worker %d", thread_data1);
	if(kt[0]){
		kthread_bind(kt[0], (thread_data1 + 1) % NUM_CORE);
		trace_printk("jie: init->worker thread %d (id %d) on cpu %d create successfully!\n", 
		thread_data1, kt[0]->pid, kt[0]->on_cpu);

	}
	kt[1] = kthread_create(worker, (void *)&thread_data2, "worker %d", thread_data2);
	if(kt[1]){
		kthread_bind(kt[1], (thread_data2 + 1) % NUM_CORE);
		trace_printk("jie: init->worker thread %d (id %d) on cpu %d create successfully!\n",
		thread_data2, kt[1]->pid, kt[1]->on_cpu);

	}
	smp_mb();
	trace_printk("jie: init->about to launch the kernel threads...\n");	
	wake_up_process(kt[0]);
	wake_up_process(kt[1]);
	init_flag = 1;
	retval = 0;
	return retval;
}

SYSCALL_DEFINE4(asyncos_issue_read, unsigned int, fd, char __user *, buf, size_t, count, int, q_index){
	size_t retval = 0;
	struct syscall_entry *e;
	trace_printk(KERN_INFO "jie: issue->asyncos_issue_read!\n");
	/* potentially we can spawn multiple threads (or pin the threads on specific cores)
	 * and then run the sys_read on different threads*/
	/* poplulate the queue entry*/	
	retval = __get_free_entry(q_index);
	e = &sys_queue[q_index].sq[retval];
	e->sys_num = 0;
	e->arg_cnt = 3;
	e->arg1 = (int64_t)fd;
	e->arg2 = (unsigned long)buf;
	e->arg3 = (unsigned long)count;
	smp_wmb();
	trace_printk(KERN_INFO "jie: issue->read on fd: %d, buf: 0x%lx, reading %d bytes, returning %d, pid %d\n",
	(int)e->arg1, (unsigned long)e->arg2, (int)e->arg3, (int)retval, (int)sys_getpid());
	//retval = sys_read((unsigned int)e->arg1,(char __user *)e->arg2,(size_t)e->arg3);
	//trace_printk(KERN_INFO"***read returns %d\n", retval);
	//return retval;
	/* copy and transfer the fs and file struct information*/
	files[q_index] = current->files;
	fs[q_index] = current->fs;
	trace_printk(KERN_INFO "jie: issue->files 0x%lx, fs 0x%lx\n",(unsigned long)current->files, (unsigned long)current->fs);
	if(cas(SC_ALLOCATED, SC_POSTED, &e->status)){
		return retval;
	}
	//retval = sys_read(fd, buf, count);
	return -1;
}

SYSCALL_DEFINE2(asyncos_complete, int, q_index, int, pos){
	trace_printk("jie: complete->asyncos_complete! q_index: %d, pos: %d\n",q_index, pos);
	return __complete(q_index, pos);
}

