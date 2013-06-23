
#ifndef __ACL_WORK_QUEUE_H_INCLUDED__
#define __ACL_WORK_QUEUE_H_INCLUDED__

#include "stdlib/acl_define.h"
#include <time.h>

#ifdef	ACL_UNIX
#include <pthread.h>
#endif
#include "acl_task.h"

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * 该结构由工作线程产生并维护，用户可以在调用 acl_workq_atinit() 时所注册的回调
 * 函数 worker_init_fn() 被调用时作为一个指针参数传递给用户，用户可以将工作线程
 * 启动初始化函数(即所回调的 worker_init_fn ())中将自己的初始化变量对象赋值给该
 * 结构指针中的 init_data 成员变量，这样当工作线程在进行任务处理(即调用worker_fn())
 * 时会将用户给 init_data 的成员赋值变量由 ACL_WORKER_ATTR 结构指针带给用户工作函数.
 */
typedef struct ACL_WORKER_ATTR {
#ifdef	ACL_UNIX
	pthread_t id;		/* 当前工作线程的线程号 */
#elif	defined(ACL_MS_WINDOWS)
	unsigned long id;	/* 当前工作线程的线程号 */
#else
# error "unknown OS"
#endif
	unsigned int count;	/* 当前工作线程的任务处理次数 */
	time_t begin_t;		/* 当前工作线程开始启动时的时间 */
	time_t end_t;		/* 当前工作线程退出时的时间 */
	time_t idle_t;		/* 当前工作线程的空闲等待时间 */
	void *init_data;	/* 用户在线程初始化时传递过来的特殊变量 */
	void *free_data;	/* 线程退出时用户传递过来的变量 */
	void *run_data;	
} ACL_WORKER_ATTR;

/* #define	ACL_THREAD_IDLE_TIMEOUT		10 */

typedef struct ACL_WORK_ELEMENT ACL_WORK_ELEMENT;
typedef struct ACL_WORK_QUEUE   ACL_WORK_QUEUE;

/**
 * 初始化一个线程池对象
 * @param threads 该线程池对象的最大线程数
 * @param idle_timeout 工作线程的空闲时间，单位为秒
 * @param poller_fn 循环检测任务队列的回调函数
 * @param poller_arg poller_fn 所需要的参数
 * @return 返回 ACL_WORK_QUEUE 类型的指针, 如果不为空则表示成功，否则失败
 */
ACL_API ACL_WORK_QUEUE *acl_workq_create(int threads,
					int idle_timeout,
					int (*poller_fn)(void *),
					void *poller_arg);

/**
 * 当队列堆积的任务数大于空闲线程数的2倍时. 通过此函数设置添加任务的
 * 线程休眠时间, 如果不调用此函数进行设置, 则添加线程不会进入休眠状态.
 * @param wq 线程池对象，不能为空
 * @param timewait_sec 休眠　的时间值, 建议将此值设置为 1--5 秒内
 * @return 成功返回 0, 失败返回 -1
 */
ACL_API int acl_workq_set_timewait(ACL_WORK_QUEUE *wq, int timewait_sec);

/**
 * 添加注册函数，在线程创建后立即执行此初始化函数
 * @param wq 线程池对象，不能为空
 * @param worker_init_fn 工作线程初始化函数, 如果该函数返回 < 0, 则该线程自动退出。
 * @param worker_init_arg worker_init_fn 所需要的参数
 * @return == 0, OK; != 0, Err.
 * 注: acl_workq_atinit() 应在调用 acl_workq_create() 后立即调用
 *     worker_free_fn 中的第二个参数为ACL_WORKER_ATTR 结构指针，由线程池的某个
 *     工作线程生成，用户可以将自己的特殊数据类型赋值为该结构指针的 init_data 成员,
 *     如：连接池对象等。
 */
ACL_API int acl_workq_atinit(ACL_WORK_QUEUE *wq,
				int (*worker_init_fn)(void *, ACL_WORKER_ATTR *),
				void *worker_init_arg);

/**
 * 添加注册函数，在线程退出立即执行此初函数
 * @param wq 线程池对象，不能为空
 * @param worker_free_fn 工作线程退出前必须执行的函数
 * @param worker_free_arg worker_free_fn 所需要的参数
 * @return == 0, OK; != 0, Err.
 * 注: acl_workq_atfree() 应在调用 acl_workq_create() 后立即调用
 *     worker_free_fn 中的第二个参数为ACL_WORKER_ATTR 结构指针，由线程池的某个
 *     工作线程生成，用户可以将自己的特殊数据类型赋值为该结构指针的 init_data 成员,
 *     如：连接池对象等。
 */
ACL_API int acl_workq_atfree(ACL_WORK_QUEUE *wq,
				void (*worker_free_fn)(void *, ACL_WORKER_ATTR *),
				void *worker_free_arg);

/**
 * 销毁一个线程池对象, 成功销毁后该对象不能再用.
 * @param wq 线程池对象，不能为空
 * @return 0 成功; != 0 失败
 */
ACL_API int acl_workq_destroy(ACL_WORK_QUEUE *wq);

/**
 * 暂停一个线程池对象的运行, 停止后还可以再运行.
 * @param wq 线程池对象，不能为空
 * @return 0 成功; != 0 失败
 */
ACL_API int acl_workq_stop(ACL_WORK_QUEUE *wq);

/**
 * 添加一个任务
 * @param wq 线程池对象，不能为空
 * @param worker_fn 当有可用工作线程时所调用的回调处理函数
 * @param worker_arg 回调函数 worker_fn() 所需要的回调参数
 * @param event_type 事件类型，一般不必关心此值
 * @return 0 成功; != 0 失败
 * 注：worker_fn 中的第二个参数为ACL_WORKER_ATTR结构指针，由线程池的某个
 *     工作线程维护，该结构指针中的成员变量 init_data 为用户的赋值传送变量，
 *     如：数据库连接对象等。
 */
ACL_API int acl_workq_add(ACL_WORK_QUEUE *wq,
			void (*worker_fn)(int, void *),
			int  event_type,
			void *worker_arg);

/**
 * 开始进行批处理方式的添加任务, 实际上是开始进行加锁
 * @param wq 线程池对象，ACL_WORK_QUEUE 类型的结构指针, 不能为空
 */
ACL_API void acl_workq_batadd_begin(void *wq);

/**
 * 添加一个新任务, 前提是已经成功加锁, 即调用 cl_workq_batadd_begin 成功
 * @param wq 线程池对象，不能为空
 * @param worker_fn 当有可用工作线程时所调用的回调处理函数
 * @param worker_arg 回调函数 worker_fn() 所需要的回调参数
 * @param event_type 事件类型，一般不必关心此值
 * 注：worker_fn 中的第二个参数为ACL_WORKER_ATTR结构指针，由线程池的某个
 *     工作线程维护，该结构指针中的成员变量 init_data 为用户的赋值传送变量，
 *     如：数据库连接对象等。
 */
ACL_API void acl_workq_batadd_one(ACL_WORK_QUEUE *wq,
		void (*worker_fn)(int, void *),
		int  event_type,
		void *worker_arg);
/**
 * 批处理添加结束
 * @param wq 线程池对象，ACL_WORK_QUEUE 类型的结构指针, 不能为空
 */
ACL_API void acl_workq_batadd_end(void *wq);

/**
 * 启动一个线程池任务对象
 * @param wq 线程池对象，不能为空
 * @return 0 成功; != 0 失败, 可以对返回值调用 strerror(ret) 取得错误原因描述
 */
ACL_API int acl_workq_start(ACL_WORK_QUEUE *wq);

/**
 * 以批处理方式进行任务的分发, 其内部其实是调用了 acl_workq_batadd_one()
 * @param dispatch_arg 批量添加时的参数
 * @param worker_fn 任务回调函数指针
 * @param event_type 事件类型，一般不必关心此值
 * @param worker_arg worker_fn 参数之一
 * @return 0: OK; -1: err
 * 注：worker_fn 中的第二个参数为ACL_WORKER_ATTR结构指针，由线程池的某个
 *     工作线程维护，该结构指针中的成员变量 init_data 为用户的赋值传送变量，
 *     如：数据库连接对象等。
 */
ACL_API int acl_workq_batadd_dispatch(void *dispatch_arg,
		void (*worker_fn)(int, void *),
		int  event_type,
		void *worker_arg);

/**
 * 以单个添加的方式进行任务的分发
 * @param dispatch_arg 批量添加时的参数
 * @param worker_fn 任务回调函数指针
 * @param event_type 事件类型，一般不必关心此值
 * @param worker_arg worker_fn 参数之一
 * @return 0: OK; -1: err
 * 注：worker_fn 中的第二个参数为ACL_WORKER_ATTR结构指针，由线程池的某个
 *     工作线程维护，该结构指针中的成员变量 init_data 为用户的赋值传送变量，
 *     如：数据库连接对象等。
 */
ACL_API int acl_workq_dispatch(void *dispatch_arg,
		void (*worker_fn)(int, void *),
		int  event_type,
		void *worker_arg);

/**
 * 此函数目前未用
 * @param wq 线程池对象，不能为空
 * @deprecated
 */
ACL_API int acl_workq_quit_wait(ACL_WORK_QUEUE *wq);

/**
 * 当前线程池中的线程数
 * @param wq 线程池对象，不能为空
 * @return 返回线程池中的总线程数
 */
ACL_API int acl_workq_nworker(ACL_WORK_QUEUE *wq);

/**
 * 设置线程池中线程的堆栈大小
 * @param stacksize {size_t} 线程创建时的堆栈大小，单位为字节
 */
ACL_API void acl_workq_set_stacksize(size_t stacksize);

#ifdef	__cplusplus
}
#endif

#endif	/* !__ACL_WORK_QUEUE_H_INCLUDED__ */


