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

struct syscall_queue{
	struct syscall_entry sq[QUEUE_SIZE] __attribute__ ((aligned (CACHE_LINE_SIZE_X86_64)));
};

struct syscall_queue sys_queue[NUM_CORE] __attribute__ ((aligned (CACHE_LINE_SIZE_X86_64)));

enum queue_entry_status{
	FREE,
	ALLOCATED,
	POSTED,
	COMPLETE
};


static struct task_struct *kt1, *kt2;
static uchar init_flag = 0;
static int thread_data1 = 0;
static int thread_data2 = 0;

/* worker thread to take over specific syscall queue*/
static inline int worker(void *data){
	int retval = -EPERM;
	int q_index = *((int *)data);
	asm_mb();
	printk("jie: kernel thread for %d-th syscall queue\n", q_index);
	/* the following code will work on sys_queue[q_index];*/
	while(1){
		relax();
	}	
	
	/* is supposed not to be here*/
	return retval;
}


SYSCALL_DEFINE0(asyncos_init){
	size_t retval = -EPERM;
	thread_data1 = 0;
	thread_data2 = 1;
	asm_mb();
	printk("jie: asyncos_init flag %d\n", init_flag);
	if(init_flag == 1){
		printk("jie: asyncos_init is already called\n");
		return retval;
	}
	printk(KERN_INFO "jie: asyncos_init !\n");
	/* create two worker threads, once create, they cannot be terminated*/
	BUG_ON((thread_data1 > 3 || thread_data2 > 3));
	asm_mb();
	kt1 = kthread_create(worker, (void *)&thread_data1, "worker %d", thread_data1);
	if(kt1){
		printk("jie: worker thread %d (id %d) on cpu %d create successfully...lanuch it\n", 
		thread_data1, kt1->pid, kt1->on_cpu);
		kthread_bind(kt1, (thread_data1 + 1) % NUM_CORE);
		wake_up_process(kt1);
	}
	asm_mb();
	kt2 = kthread_create(worker, (void *)&thread_data2, "worker %d", thread_data2);
	if(kt2){
		kthread_bind(kt2, (thread_data2 + 1) % NUM_CORE);
		wake_up_process(kt2);
		printk("jie: worker thread %d (id %d) on cpu %d create successfully...lanuch it\n",
		thread_data2, kt2->pid, kt2->on_cpu);

	}
	asm_mb();
	init_flag = 1;
	retval = 0;
	return retval;
}

SYSCALL_DEFINE3(asyncos_issue_read, unsigned int, fd, char __user *, buf, size_t, count){
	size_t retval = 0;
	printk(KERN_INFO "jie: asyncos_issue_read!\n");
	/* potentially we can spawn multiple threads (or pin the threads on specific cores)
	 * and then run the sys_read on different threads*/
	retval = sys_read(fd, buf, count);
	printk(KERN_INFO "jie: read on fd: %d, reading %lu bytes, returning %lu\n", fd, count, retval);
	return retval;
}

SYSCALL_DEFINE2(asyncos_complete, int, worker, int, pos){
	printk("jie: asyncos_complete! worker: %d, pos: %d\n",worker, pos);
	return 0;
}

