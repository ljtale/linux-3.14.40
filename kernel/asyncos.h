/* asyncos.h header file for asyncos.c
 **/

typedef void (*sys_call_ptr_t)(void);
extern const sys_call_ptr_t sys_call_table[];

/* architecture specific*/
#define CACHE_LINE_SIZE_X86_64	64

#ifndef true
#define true	1
#endif

#ifndef false
#define false	0
#endif

typedef unsigned char uchar;


struct syscall_entry{
	int syscall_num;	/* 4 bytes*/
	int retval;		/* 4 bytes*/
	unsigned long arg1;	/* 8 bytes*/
	unsigned long arg2;	/* 8 bytes*/
	unsigned long arg3;	/* 8 bytes*/
	unsigned long arg4;	/* 8 bytes*/
	unsigned long arg5;	/* 8 bytes*/
	unsigned long arg6;	/* 8 bytes*/
	char __padding[64];
};


static inline void relax(void)
{
        __asm__ __volatile__ ( "pause" ::: "memory" );
}

static inline void asm_mb(void)
{
	__asm__ __volatile__ ( "" ::: "memory" );
}
