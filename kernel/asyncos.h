/* asyncos.h header file for asyncos.c
 **/


typedef void (*sys_call_ptr_t)(void);
extern const sys_call_ptr_t sys_call_table[];
