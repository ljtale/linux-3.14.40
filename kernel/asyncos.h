/* asyncos.h header file for asyncos.c
 **/

#include <stdbool.h>

typedef void (*sys_call_ptr_t)(void);
extern const sys_call_ptr_t sys_call_table[];

typedef signed long long int int64_t;
typedef unsigned long long int uint64_t;

/* architecture specific*/
#define CACHE_LINE_SIZE_X86_64	64

typedef unsigned char uchar;


struct syscall_entry{
	int status;		/* 4 bytes*/
	int sys_num;		/* 4 bytes*/
	/* for generic syscalls identification, we need to reuse arg1 as the retval*/
//	unsigned long retval;	/* 8 bytes*/
	int arg_cnt;		/* 4 bytes*/
	int something;		/* 4 bytes*/ 
	int64_t arg1;		/* 8 bytes*/
	unsigned long arg2;	/* 8 bytes*/
	unsigned long arg3;	/* 8 bytes*/
	unsigned long arg4;	/* 8 bytes*/
	unsigned long arg5;	/* 8 bytes*/
	unsigned long arg6;	/* 8 bytes*/
};


static inline void relax(void)
{
        __asm__ __volatile__ ( "pause" ::: "memory" );
}

static inline void asm_mb(void)
{
	__asm__ __volatile__ ( "" ::: "memory" );
}

static inline bool cas(int o, int n, int *p){

	return __sync_bool_compare_and_swap(p, o, n);
}

static inline int __get_free_entry(int q_index);
static inline int64_t __complete(int q_index, int pos);
