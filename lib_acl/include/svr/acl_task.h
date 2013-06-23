#ifndef	__TASK_INCUDE_H_
#define	__TASK_INCUDE_H_

#ifdef	__cplusplus
extern "C" {
#endif

#include "stdlib/acl_define.h"
#include "thread/acl_pthread.h"

typedef struct ACL_TASK_NODE {
	int   status;		/* defined above ACL_TASK_STATUS_ */
	void  (*task_fn)(void *task_node);
	int   flag_block;	/* defined above ACL_TASK_STATUS_ */
	int   max_block_timo;
#ifdef	ACL_UNIX
	pid_t mypid;
#endif
	acl_pthread_t mytid;
	char *pfunc_name;
	void *data;		/* application data */
} ACL_TASK_NODE;

#define	ACL_TASK_STATUS_UNKOWN	0x0000
#define	ACL_TASK_STATUS_BLOCK	0x0001

#define	ACL_SET_TASK_STATE(x, status_in) do {                                \
	ACL_TASK_NODE *__x = x;                                              \
	__x->status = status_in;                                             \
} while(0)

#define	ACL_SET_TASK_BLOCK(x) do {                                           \
	ACL_TASK_NODE *__x = x;                                              \
	__x->status |= ACL_TASK_STATUS_BLOCK;                                \
} while(0)

#define	ACL_SET_TASK_NBLOCK(x) do {                                          \
	ACL_TASK_NODE *__x = x;                                              \
	__x->status &= ~ACL_TASK_STATUS_BLOCK;                               \
} while(0)

#ifdef	ACL_UNIX
#define	ACL_IS_TASK_BLOCK(x)                                                 \
	(__extension__                                                       \
	 ({                                                                  \
	   ACL_TASK_NODE *__x = x;                                           \
	   register char __ret;                                              \
                                                                             \
           __ret = __x->status & ACL_TASK_STATUS_BLOCK;                      \
           __ret; }))

#define	ACL_IS_TASK_NBLOCK(x)	!ACL_IS_TASK_BLOCK(x)
#endif

typedef int (*ACL_TASK_DISPATCH) (ACL_TASK_NODE *context);	/* callback function */

#ifdef	__cplusplus
}
#endif

#endif


