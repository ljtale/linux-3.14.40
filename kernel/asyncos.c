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


static struct task_struct *kt1, *kt2;
static uchar init_flag = 0;
static int thread_data1 = 0;
static int thread_data2 = 0;


/* worker thread to take over specific syscall queue*/
static inline int 
worker(void *data){
	int retval = -EPERM;
	int q_index = *((int *)data);
	struct syscall_entry *e;
	ssize_t sys_ret = -1;
	smp_rmb();
	printk("jie: worker->kernel thread for %d-th syscall queue\n", q_index);
	/* the following code will work on sys_queue[q_index];*/
	for(;;){
		int q;
		/* spin on the corresponding syscall queue to check the pending syscalls*/
		do{
			for(q = 0; q < QUEUE_SIZE; q++){
				if(sys_queue[q_index].sq[q].status == SC_POSTED){
					if(cas(SC_POSTED, SC_ACQUIRED,&sys_queue[q_index].sq[q].status)){
						e = &sys_queue[q_index].sq[q];
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
	printk("jie: worker->arg1 %u, arg2 0x%lx, arg3 %lu\n",
	(unsigned int)e->arg1, (unsigned long)e->arg2, (size_t)e->arg3);
	sys_ret = sys_read((unsigned int)e->arg1, (char __user *)e->arg2, (size_t)e->arg3);
	printk("jie: worker-> sys_read returns %d\n", (int)sys_ret);
	smp_wmb();
	e->status = sys_ret >=0 ? SC_COMPLETE : SC_FREE;
	smp_wmb();
#else
	/* FIXME: the following code is not working at present*/
/*
	printk("jie: worker->syscall num %d, syscall arg cnt %d\n", e->sys_num, e->arg_cnt);
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
	}
	/* is supposed not to be here*/
//stop:
	return retval;
}


static inline int 
__get_free_entry(int q_index){
	unsigned long i;
	do{
		for( i = 2; i < QUEUE_SIZE; i++ ){
			if(cas(SC_FREE, SC_ALLOCATED, &sys_queue[q_index].sq[i].status)){
				printk("jie: get_free_entry-> get entry %lu\n",i);
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
	do{
		if(e->status == SC_COMPLETE){
			printk("jie: complete-> complete pos %d in queue %d\n", pos, q_index);
			retval = (int64_t)e->arg1;
			smp_mb();
			if(cas(SC_COMPLETE, SC_FREE, &e->status)){
				return retval;
			}
		}
	}while(1);
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
	printk("jie: init-> asyncos_init flag %d\n", init_flag);
	if(init_flag == 1){
		printk("jie: init->asyncos_init is already called\n");
		return retval;
	}
	printk(KERN_INFO "jie: init->asyncos_init !\n");
	printk(KERN_INFO "jie: init->system call table address: 0x%lx\n",(unsigned long)(sys_call_table));
	/* create two worker threads, once create, they cannot be terminated*/
	BUG_ON((thread_data1 > 3 || thread_data2 > 3));
	kt1 = kthread_create(worker, (void *)&thread_data1, "worker %d", thread_data1);
	if(kt1){
		kthread_bind(kt1, (thread_data1 + 1) % NUM_CORE);
		printk("jie: init->worker thread %d (id %d) on cpu %d create successfully!\n", 
		thread_data1, kt1->pid, kt1->on_cpu);

	}
	kt2 = kthread_create(worker, (void *)&thread_data2, "worker %d", thread_data2);
	if(kt2){
		kthread_bind(kt2, (thread_data2 + 1) % NUM_CORE);
		printk("jie: init->worker thread %d (id %d) on cpu %d create successfully!\n",
		thread_data2, kt2->pid, kt2->on_cpu);

	}
	smp_mb();
	printk("jie: init->about to launch the kernel threads...\n");	
	wake_up_process(kt2);
	wake_up_process(kt1);
	init_flag = 1;
	retval = 0;
	return retval;
}

SYSCALL_DEFINE4(asyncos_issue_read, unsigned int, fd, char __user *, buf, size_t, count, int, q_index){
	size_t retval = 0;
	struct syscall_entry *e;
	printk(KERN_INFO "jie: issue->asyncos_issue_read!\n");
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
	printk(KERN_INFO "jie: issue->read on fd: %d, buf: 0x%lx, reading %d bytes, returning %d\n",
	(int)e->arg1, (unsigned long)e->arg2, (int)e->arg3, (int)retval);
	//retval = sys_read((unsigned int)e->arg1,(char __user *)e->arg2,(size_t)e->arg3);
	//printk(KERN_INFO"***read returns %d\n", retval);
	//return retval;
	if(cas(SC_ALLOCATED, SC_POSTED, &e->status)){
		return retval;
	}
	//retval = sys_read(fd, buf, count);
	return -1;
}

SYSCALL_DEFINE2(asyncos_complete, int, q_index, int, pos){
	printk("jie: complete->asyncos_complete! q_index: %d, pos: %d\n",q_index, pos);
	return __complete(q_index, pos);
}

