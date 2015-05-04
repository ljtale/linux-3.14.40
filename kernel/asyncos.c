/* asyncos.c inter-syscall parallelism kernel support, asynchronous syscall execution
 **/

#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>

#include "asyncos.h"

SYSCALL_DEFINE3(asyncos_sys1, unsigned int, fd, char __user *, buf, size_t, count){
	size_t retval = 0;
	printk(KERN_INFO "jie: this is the new asyncos syscall !\n");
	return retval;
}
