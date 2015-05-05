/* asyncos.c inter-syscall parallelism kernel support, asynchronous syscall execution
 **/

#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <linux/kthread.h>
#include <linux/spinlock.h>
#include <asm/errno.h>


#include "asyncos.h"

#define QUEUE_SIZE	8
#define NUM_CORE	4

static struct syscall_entry syscall_queue[QUEUE_SIZE] __attribute__ ((aligned (CACHE_LINE_SIZE_X86_64)));
static struct task_struct *kt1, *kt2;


/* worker thread to take over specific syscall queue*/
static inline int worker(void *data){
	int retval = -EPERM;
	int q_index = *((int *)data);
	printk("jie: kernel thread for %d-th syscall queue\n", q_index);
	syscall_queue[q_index].syscall_num = 0;
	while(1){
		relax();
	}	
	
	/* is supposed not to be here*/
	return retval;
}


SYSCALL_DEFINE3(asyncos_sys1, unsigned int, fd, char __user *, buf, size_t, count){
	size_t retval = -EPERM;
	int data = 0;
	printk(KERN_INFO "jie: this is the new asyncos syscall 1 !\n");
	/* create two worker threads, once create, they cannot be terminated*/
	data = 1;
	kt1 = kthread_create(worker, &data, "worker 1");
	if(kt1){
		printk("jie: worker 1 create successfully...lanuch it\n");
		wake_up_process(kt1);
	}
	data = 2;
	kt2 = kthread_create(worker, &data, "worker 2");
	if(kt2){
		printk("jie: worker 2 create successfully...lanuch it\n");
		wake_up_process(kt2);
	}
	retval = 0;
	return retval;
}

SYSCALL_DEFINE3(asyncos_sys2, unsigned int, fd, char __user *, buf, size_t, count){
	size_t retval = 0;
	printk(KERN_INFO "jie: this is the new asyncos syscall 2 used to replace read()!\n");
	/* potentially we can spawn multiple threads (or pin the threads on specific cores)
	 * and then run the sys_read on different threads*/
	retval = sys_read(fd, buf, count);
	printk(KERN_INFO "jie: read on fd: %d, reading %lu bytes, returning %lu\n", fd, count, retval);
	return retval;
}
