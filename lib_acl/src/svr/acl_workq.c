#include "StdAfx.h"
#ifndef ACL_PREPARE_COMPILE

#include "stdlib/acl_define.h"
#include <errno.h>
#ifdef	ACL_UNIX
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#elif	defined(ACL_MS_WINDOWS)
#include <time.h>

#ifdef ACL_MS_VC
#pragma once
#endif

#endif

#ifdef ACL_BCB_COMPILER
#pragma hdrstop
#endif

#include "stdlib/acl_sys_patch.h"
#include "stdlib/acl_msg.h"
#include "thread/acl_pthread.h"
#include "stdlib/acl_mymalloc.h"
#include "stdlib/acl_debug.h"
#include "svr/acl_workq.h"

#endif

#define	ACL_WORK_QUEUE_VALID		0x0decca62
static size_t __thread_stacksize = 0;

struct ACL_WORK_ELEMENT {
	struct ACL_WORK_ELEMENT *next;
	void (*worker_fn)(int event_type, void *arg);	/* user function	*/
	int   event_type;				/* come from event	*/
	void  *worker_arg;
};

struct ACL_WORK_QUEUE {
	acl_pthread_mutex_t	worker_mutex;	/* control access to queue	*/
	acl_pthread_cond_t	worker_cond;	/* wait_for_work		*/
	acl_pthread_mutex_t	poller_mutex;	/* just for wait poller exit	*/
	acl_pthread_cond_t	poller_cond;	/* just for wait poller exit	*/
	acl_pthread_attr_t	attr;		/* create detached		*/
	ACL_WORK_ELEMENT *first, *last;	/* work queue			*/
	int   poller_running;		/* is poller thread running ?	*/
	int   qlen;			/* the work queue's length	*/
	int   valid;			/* valid			*/
	int   worker_quit;		/* worker should quit		*/
	int   poller_quit;		/* poller should quit		*/
	int   parallelism;		/* maximum threads		*/
	int   counter;			/* current threads		*/
	int   idle;			/* idle threads			*/
	int   idle_timeout;		/* idle timeout second		*/
	int   overload_timewait;	/* when too busy, need sleep ?	*/
	time_t last_warn;               /* last warn time               */
	int  (*poller_fn)(void *arg);	/* worker poll function		*/
	void *poller_arg;		/* the arg of poller_fn		*/
	int  (*worker_init_fn)(void *arg, ACL_WORKER_ATTR *);
	void *worker_init_arg;
	void (*worker_free_fn)(void *arg, ACL_WORKER_ATTR *);
	void *worker_free_arg;
};

#undef	__SET_ERRNO
#ifdef	ACL_MS_WINDOWS
# define	__SET_ERRNO(_x_) (void) 0
#elif	defined(ACL_UNIX)
# define	__SET_ERRNO(_x_) (acl_set_error(_x_))
#else
# error "unknown OS type"
#endif

#ifdef	ACL_MS_WINDOWS
#define	sleep(_x_) do {  \
	Sleep(_x_ * 1000);  \
} while (0)
#endif

static void *__poller_thread(void *arg)
{
	char  myname[] = "__poller_thread";
	ACL_WORK_QUEUE   *wq = (ACL_WORK_QUEUE*) arg;
	const int wait_sec = 1, max_loop_persec = 81920;
	int   loop_count;
	int   status;
	char  buf[256];
	time_t now_t, pre_loop_t;
#ifdef	ACL_UNIX
	pthread_t id = pthread_self();
#elif	defined(ACL_MS_WINDOWS)
	unsigned long id = acl_pthread_self();
#else
        # error "unknown OS"
#endif

	if (wq->poller_fn == NULL)
		acl_msg_fatal("%s, %s(%d): poller_fn is null!",
			__FILE__, myname, __LINE__);

	acl_debug(ACL_DEBUG_WQ, 2) ("%s(%d): poller(tid=%lu) started ...",
			myname, __LINE__, (unsigned long) id);
	loop_count = 0;
	pre_loop_t = time(NULL);

	status = acl_pthread_mutex_lock(&wq->poller_mutex);
	if (status != 0) {
		wq->poller_running = 0;
		__SET_ERRNO(status);
		acl_msg_error("%s, %s(%d): pthread_mutex_lock error(%s)",
				__FILE__, myname, __LINE__,
				acl_last_strerror(buf, sizeof(buf)));
		return (NULL);
	}

	wq->poller_running = 1;

	for (;;) {
		if (wq->poller_quit)
			break;
		
		now_t = time(NULL);
		loop_count++;
		if (loop_count >= max_loop_persec) {
			/* avoid loop too quickly in one second */
			if (now_t - pre_loop_t <= wait_sec) {
				acl_msg_warn("%s: loop too fast, sleep %d second",
						myname, wait_sec);
				sleep(wait_sec);
				now_t = time(NULL);  /* adjust the time of now */
			}
			pre_loop_t = now_t;  /*adjust the pre_loop_t time */
			loop_count = 0;
		}

		if (wq->poller_fn(wq->poller_arg) < 0)
			break;
	}

	acl_debug(ACL_DEBUG_WQ, 2) ("%s(%d): poller(%lu) thread quit ...",
			myname, __LINE__, (unsigned long) id);

	wq->poller_running = 0;
		
	status = acl_pthread_cond_signal(&wq->poller_cond);
	/* status = pthread_cond_broadcast(&wq->poller_cond); */

	if ( status != 0 ) {
		__SET_ERRNO(status);
		acl_msg_error("%s(%d)->%s: pthread_cond_signal, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
	}

	acl_debug(ACL_DEBUG_WQ, 3) ("poller broadcast ok");

	status = acl_pthread_mutex_unlock(&wq->poller_mutex);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_error("%s, %s(%d): pthread_mutex_unlock error(%s)",
				__FILE__, myname, __LINE__,
				acl_last_strerror(buf, sizeof(buf)));
		return (NULL);
	}

	acl_debug(ACL_DEBUG_WQ, 3) ("poller unlock ok");

	return (NULL);
}

static void *__worker_thread(void* arg)
{
	char  myname[] = "__worker_thread";
	struct	timespec  timeout;
	ACL_WORK_QUEUE 	 *wq = (ACL_WORK_QUEUE*) arg;
	ACL_WORK_ELEMENT *we;
	struct  timeval   tv;
	int  status, timedout;
	ACL_WORKER_ATTR worker_attr;
	time_t  wait_begin_t;
	char  buf[256];

#undef	RETURN
#define	RETURN(_x_) {  \
	worker_attr.end_t = time(NULL); \
	if (wq->worker_free_fn != NULL)  \
		wq->worker_free_fn(wq->worker_free_arg, &worker_attr);  \
	return (_x_);  \
}

	memset(&worker_attr, 0, sizeof(ACL_WORKER_ATTR));
	worker_attr.id = acl_pthread_self();
	worker_attr.begin_t = time(NULL);
	worker_attr.init_data = wq->worker_init_arg;
	worker_attr.free_data = wq->worker_free_arg;
	
	if (wq->worker_init_fn != NULL) {
		if (wq->worker_init_fn(wq->worker_init_arg, &worker_attr) < 0) {
			acl_msg_error("%s(%d)->%s: sid = %lu, worker init call error",
					__FILE__, __LINE__, myname,
					(unsigned long) worker_attr.id);
			return (NULL);
		}
	}

	status = acl_pthread_mutex_lock(&wq->worker_mutex);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_error("%s(%d)->%s: sid = %lu, lock error(%s)",
				__FILE__, __LINE__, myname,
				(unsigned long) worker_attr.id,
				acl_last_strerror(buf, sizeof(buf)));
		return (NULL);
	}
	
	for (;;) {
		gettimeofday(&tv, NULL);
		timeout.tv_sec = tv.tv_sec + wq->idle_timeout;
		timeout.tv_nsec = tv.tv_usec * 1000;
		wait_begin_t = time(NULL);
		timedout = 0;

		while (wq->first == NULL && !wq->worker_quit) {
			wq->idle++;

			if (wq->idle_timeout > 0)
				status = acl_pthread_cond_timedwait(&wq->worker_cond,
						&wq->worker_mutex, &timeout);
			else
				status = acl_pthread_cond_wait(&wq->worker_cond,
						&wq->worker_mutex);
			wq->idle--;
			if (status == ACL_ETIMEDOUT) {
				timedout = 1;
				break;
			} else if (status != 0) {
				__SET_ERRNO(status);
				wq->counter--;
				acl_pthread_mutex_unlock(&wq->worker_mutex);
				acl_msg_error("%s(%d)->%s: sid = %lu,"
					" cond timewait error(%s)(%s)",
					__FILE__, __LINE__, myname,
					(unsigned long) worker_attr.id,
					acl_last_strerror(buf, sizeof(buf)),
					strerror(status));
				RETURN (NULL);
			}
		}  /* end while */
		we = wq->first;
		if (we != NULL) {
			if (we->worker_fn == NULL)
				acl_msg_fatal("%s(%d)->%s: worker_fn null",
						__FILE__, __LINE__, myname);

			wq->first = we->next;
			wq->qlen--;
			if (wq->last == we)
				wq->last = NULL;
			/* the lock shuld be unlocked before enter working processs */
			status = acl_pthread_mutex_unlock(&wq->worker_mutex);
			if (status != 0) {
				__SET_ERRNO(status);
				acl_msg_error("%s(%d)->%s: unlock error(%s), sid=%lu",
						__FILE__, __LINE__, myname,
						acl_last_strerror(buf, sizeof(buf)),
						(unsigned long) worker_attr.id);
				RETURN (NULL);
			}
			worker_attr.count++;
			worker_attr.idle_t = time(NULL) - wait_begin_t;
			worker_attr.run_data = we->worker_arg;
			we->worker_fn(we->event_type, (void *) &worker_attr);
			acl_myfree(we);

			/* lock again */
			status = acl_pthread_mutex_lock(&wq->worker_mutex);
			if (status != 0) {
				__SET_ERRNO(status);
				acl_msg_error("%s(%d)->%s: lock error(%s), sid=%lu",
						__FILE__, __LINE__, myname,
						acl_last_strerror(buf, sizeof(buf)),
						(unsigned long) worker_attr.id);
				RETURN (NULL);
			}
		}
		if (wq->first == NULL && wq->worker_quit) {
			wq->counter--;
			if (wq->counter == 0)
				acl_pthread_cond_broadcast(&wq->worker_cond);
			break;
		}

		if (wq->first == NULL && timedout) {
			wq->counter--;
			break;
		}
	}

	status = acl_pthread_mutex_unlock(&wq->worker_mutex);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_error("%s, %s(%d): unlock error(%s)",
				__FILE__, myname, __LINE__,
				acl_last_strerror(buf, sizeof(buf)));
	}

	acl_debug(ACL_DEBUG_WQ, 2) ("%s(%d): thread(%lu) exit now",
		myname, __LINE__, (unsigned long) worker_attr.id);

	RETURN (NULL);
}

static void __init_workq(ACL_WORK_QUEUE *wq)
{
	wq->worker_quit		= 0;
	wq->poller_quit		= 0;
	wq->poller_running      = 0;
	wq->first               = NULL;
	wq->last                = NULL;
	wq->qlen		= 0;
	wq->overload_timewait	= 0;
	wq->counter		= 0;
	wq->idle		= 0;
}

/* create work queue */

ACL_WORK_QUEUE *acl_workq_create(int threads,
				int idle_timeout,
				int (*poller_fn)(void *),
				void *poller_arg)
{
	char  myname[] = "acl_workq_create";
	int   status;
	ACL_WORK_QUEUE *wq;
	char  buf[256];

	wq = acl_mycalloc(1, sizeof(*wq));
	status = acl_pthread_attr_init(&wq->attr);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_error("%s(%d)->%s: pthread_attr_init, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
		return (NULL);
	}

	if (__thread_stacksize > 0)
		acl_pthread_attr_setstacksize(&wq->attr, __thread_stacksize);

#ifdef	ACL_UNIX
	status = pthread_attr_setdetachstate(&wq->attr, PTHREAD_CREATE_DETACHED);
	if (status != 0) {
		acl_set_error(status);
		pthread_attr_destroy(&wq->attr);
		acl_myfree(wq);
		acl_msg_error("%s(%d)->%s: pthread_attr_setdetachstate, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
		return (NULL);
	}
# if     !defined(__FreeBSD__)
	status = pthread_attr_setscope(&wq->attr, PTHREAD_SCOPE_SYSTEM);
	if (status != 0) {
		pthread_attr_destroy(&wq->attr);
		acl_myfree(wq);
		acl_set_error(status);
		acl_msg_error("%s(%d)->%s: pthread_attr_setscope, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
		return (NULL);
	}
# endif
#elif defined(ACL_MS_WINDOWS)
	(void) acl_pthread_attr_setdetachstate(&wq->attr, 1);
#endif
	status = acl_pthread_mutex_init(&wq->worker_mutex, NULL);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_pthread_attr_destroy(&wq->attr);
		acl_myfree(wq);
		acl_msg_error("%s(%d)->%s: pthread_mutex_init, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
		return (NULL);
	}
	status = acl_pthread_cond_init(&wq->worker_cond, NULL);
	if (status != 0) {
		acl_pthread_attr_destroy(&wq->attr);
		acl_pthread_mutex_destroy(&wq->worker_mutex);
		acl_myfree(wq);
		__SET_ERRNO(status);
		acl_msg_error("%s(%d)->%s: pthread_cond_init, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
		return (NULL);
	}

	status = acl_pthread_mutex_init(&wq->poller_mutex, NULL);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_pthread_attr_destroy(&wq->attr);
		acl_pthread_mutex_destroy(&wq->worker_mutex);
		acl_pthread_cond_destroy(&wq->worker_cond);
		acl_myfree(wq);
		acl_msg_error("%s(%d)->%s: pthread_mutex_init, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
		return (NULL);
	}
	status = acl_pthread_cond_init(&wq->poller_cond, NULL);
	if (status != 0) {
		acl_pthread_attr_destroy(&wq->attr);
		acl_pthread_mutex_destroy(&wq->worker_mutex);
		acl_pthread_cond_destroy(&wq->worker_cond);
		acl_pthread_mutex_destroy(&wq->poller_mutex);
		acl_myfree(wq);
		__SET_ERRNO(status);
		acl_msg_error("%s(%d)->%s: pthread_cond_init, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
		return (NULL);
	}

	__init_workq(wq);

	wq->parallelism		= threads;
	wq->idle_timeout	= idle_timeout;
	wq->poller_fn		= poller_fn;
	wq->poller_arg		= poller_arg;
	
	wq->worker_init_fn	= NULL;
	wq->worker_init_arg	= NULL;
	wq->worker_free_fn	= NULL;
	wq->worker_free_arg	= NULL;

	wq->valid		= ACL_WORK_QUEUE_VALID;

	return (wq);
}

int acl_workq_set_timewait(ACL_WORK_QUEUE *wq, int timewait)
{
	char  myname[] = "acl_workq_set_timewait";
	if (wq == NULL || wq->valid != ACL_WORK_QUEUE_VALID || timewait < 0) {
		acl_msg_error("%s(%d)->%s: invalid input",
				__FILE__, __LINE__, myname);
		return (-1);
	}

	wq->overload_timewait = timewait;
	return (0);
}

int acl_workq_atinit(ACL_WORK_QUEUE *wq,
			int (*worker_init_fn)(void *, ACL_WORKER_ATTR *),
			void *worker_init_arg)
{
	char  myname[] = "acl_workq_atinit";

	if (wq == NULL || wq->valid != ACL_WORK_QUEUE_VALID) {
		acl_msg_error("%s(%d)->%s: input invalid",
				__FILE__, __LINE__, myname);
		return (ACL_EINVAL);
	}

	wq->worker_init_fn  = worker_init_fn;
	wq->worker_init_arg = worker_init_arg;

	return (0);
}

int acl_workq_atfree(ACL_WORK_QUEUE *wq,
			void (*worker_free_fn)(void *, ACL_WORKER_ATTR *),
			void *worker_free_arg)
{
	char  myname[] = "acl_workq_atfree";

	if (wq == NULL || wq->valid != ACL_WORK_QUEUE_VALID) {
		acl_msg_error("%s(%d)->%s: input invalid",
				__FILE__, __LINE__, myname);
		return (ACL_EINVAL);
	}

	wq->worker_free_fn  = worker_free_fn;
	wq->worker_free_arg = worker_free_arg;

	return (0);
}

static int __wait_poller_exit(ACL_WORK_QUEUE *wq)
{
	char  myname[] = "__wait_poller_exit";
	int   status, nwait = 0;
	char  buf[256];
	struct  timeval   tv;
	struct	timespec  timeout;

	acl_debug(ACL_DEBUG_WQ, 3) ("%s: begin to lock", myname);

	wq->poller_quit = 1;

	status = acl_pthread_mutex_lock(&wq->poller_mutex);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_error("%s, %s(%d): pthread_mutex_lock, serr = %s",
				__FILE__, myname, __LINE__,
				acl_last_strerror(buf, sizeof(buf)));
		return (status);
	}

	acl_debug(ACL_DEBUG_WQ, 3) ("%s: begin to wait cond", myname);

	while (wq->poller_running != 0) {
		gettimeofday(&tv, NULL);
		timeout.tv_sec  = tv.tv_sec + 1;
		timeout.tv_nsec = tv.tv_usec * 1000;

		nwait++;

		status = acl_pthread_cond_timedwait(&wq->poller_cond, &wq->poller_mutex, &timeout);
		if (status == ACL_ETIMEDOUT) {
			acl_debug(ACL_DEBUG_WQ, 3) ("%s: nwait=%d", myname, nwait);
		} else if (status != 0) {
			__SET_ERRNO(status);
			acl_pthread_mutex_unlock(&wq->poller_mutex);
			acl_msg_error("%s, %s(%d): pthread_cond_wait,"
					" serr = %s",
					__FILE__, myname, __LINE__,
					acl_last_strerror(buf, sizeof(buf)));
			return (status);
		}
	}

	acl_debug(ACL_DEBUG_WQ, 3) ("%s: begin to unlock", myname);

	status = acl_pthread_mutex_unlock(&wq->poller_mutex);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_error("%s, %s(%d): pthread_mutex_unlock error(%s)",
				__FILE__, myname, __LINE__,
				acl_last_strerror(buf, sizeof(buf)));
	}

	return (status);
}

static int __wait_worker_exit(ACL_WORK_QUEUE *wq)
{
	char  myname[] = "__wait_worker_exit";
	int   status, nwait = 0;
	char  buf[256];
	struct	timespec  timeout;

	status = acl_pthread_mutex_lock(&wq->worker_mutex);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_error("%s(%d)->%s: pthread_mutex_lock, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
		return (status);
	}

	wq->worker_quit = 1;

	if (wq->counter < 0) {
		acl_msg_fatal("%s(%d)->%s: counter = %d",
				__FILE__, __LINE__, myname,
				wq->counter);
	} else if (wq->counter == 0) {
		acl_debug(ACL_DEBUG_WQ, 2) ("%s: debug: counter = 0", myname);

		status = acl_pthread_mutex_unlock(&wq->worker_mutex);
		return (0);
	}

	/* 1. set quit flag
	 * 2. broadcast to wakeup any sleeping
	 * 4. wait till all quit
	 */
	/* then: wq->counter > 0 */
	
	if (wq->idle > 0) {
		acl_debug(ACL_DEBUG_WQ, 2) ("%s: idle=%d, signal idle thread", myname, wq->idle);
		status = acl_pthread_cond_broadcast(&wq->worker_cond);
		if (status != 0) {
			__SET_ERRNO(status);
			acl_pthread_mutex_unlock(&wq->worker_mutex);
			acl_msg_error("%s(%d)->%s: pthread_cond_broadcast,"
					" serr = %s",
					__FILE__, __LINE__, myname,
					acl_last_strerror(buf, sizeof(buf)));
			return (status);
		}
	}

	while (wq->counter > 0) {
		timeout.tv_sec  = 0;
		timeout.tv_nsec = 1000;

		nwait++;

		acl_debug(ACL_DEBUG_WQ, 2) ("debug(2): counter = %d, nwait=%d, idle=%d",
			wq->counter, nwait, wq->idle);

		/* status = pthread_cond_timedwait(&wq->worker_cond, &wq->worker_mutex, &timeout); */
		status = acl_pthread_cond_wait(&wq->worker_cond, &wq->worker_mutex);
		if (status == ACL_ETIMEDOUT) {
			acl_debug(ACL_DEBUG_WQ, 2) ("%s: timeout nwait=%d", myname, nwait);
		} else if (status != 0) {
			__SET_ERRNO(status);
			acl_pthread_mutex_unlock(&wq->worker_mutex);
			acl_msg_error("%s(%d)->%s: pthread_cond_timedwait,"
					" serr = %s",
					__FILE__, __LINE__, myname,
					acl_last_strerror(buf, sizeof(buf)));
			return (status);
		}
	}

	status = acl_pthread_mutex_unlock(&wq->worker_mutex);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_error("%s(%d)->%s: pthread_mutex_unlock, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
	}

	return (status);
}

int acl_workq_destroy(ACL_WORK_QUEUE *wq)
{
	char  myname[] = "acl_workq_destroy";
	int   status, s1, s2, s3, s4, s5;
	char  buf[256];

	if (wq == NULL || wq->valid != ACL_WORK_QUEUE_VALID) {
		acl_msg_error("%s(%d)->%s: input invalid",
				__FILE__, __LINE__, myname);
		return (ACL_EINVAL);
	}

	wq->valid = 0;			/* prevent any other operations	*/

	status = __wait_poller_exit(wq);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_error("%s, %s(%d): wait_poller_exit error(%s), ret=%d",
				__FILE__, myname, __LINE__,
				acl_last_strerror(buf, sizeof(buf)), status);
		return (status);
	}

	acl_debug(ACL_DEBUG_WQ, 2) ("%s(%d): poller thread exits ok, worker counter = %d",
			myname, __LINE__, wq->counter);

	status = __wait_worker_exit(wq);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_error("%s, %s(%d): wait_worker_exit error(%s), ret=%d",
				__FILE__, myname, __LINE__,
				acl_last_strerror(buf, sizeof(buf)), status);
		return (status);
	}

	acl_debug(ACL_DEBUG_WQ, 2) ("%s(%d): worker threads exit ok, conter=%d",
			myname, __LINE__, wq->counter);

	sleep(1);
	s1 = acl_pthread_mutex_destroy(&wq->poller_mutex);
	s2 = acl_pthread_cond_destroy(&wq->poller_cond);

	s3 = acl_pthread_mutex_destroy(&wq->worker_mutex);
	s4 = acl_pthread_cond_destroy(&wq->worker_cond);
	s5 = acl_pthread_attr_destroy(&wq->attr);

	acl_myfree(wq);

	status = s1 ? s1 : (s2 ? s2 : (s3 ? s3 : (s4 ? s4 : s5)));

	return (status);
}

int acl_workq_stop(ACL_WORK_QUEUE *wq)
{
	char  myname[] = "acl_workq_stop";
	int   status;
	char  buf[256];

	if (wq == NULL || wq->valid != ACL_WORK_QUEUE_VALID) {
		acl_msg_error("%s(%d)->%s: input invalid",
				__FILE__, __LINE__, myname);
		return (ACL_EINVAL);
	}

	wq->valid = 0;			/* prevent any other operations	*/

	status = __wait_poller_exit(wq);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_error("%s, %s(%d): wait_poller_exit error(%s), ret=%d",
				__FILE__, myname, __LINE__,
				acl_last_strerror(buf, sizeof(buf)), status);
		return (status);
	}

	acl_debug(ACL_DEBUG_WQ, 2) ("%s(%d): poller thread exits ok, worker counter = %d",
			myname, __LINE__, wq->counter);


	status = __wait_worker_exit(wq);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_error("%s, %s(%d): wait_worker_exit error(%s), ret=%d",
				__FILE__, myname, __LINE__,
				acl_last_strerror(buf, sizeof(buf)), status);
		return (status);
	}

	wq->valid = ACL_WORK_QUEUE_VALID;  /* restore the valid status */

	acl_debug(ACL_DEBUG_WQ, 2) ("%s(%d): worker threads exit ok, conter=%d",
			myname, __LINE__, wq->counter);

	return (0);
}

static void __workq_addone(ACL_WORK_QUEUE *wq, ACL_WORK_ELEMENT *item)
{
	char  myname[] = "__workq_addone";
	acl_pthread_t   id;
	int   status;
	char  buf[256];

	if (wq->first == NULL)
		wq->first = item;
	else
		wq->last->next = item;
	wq->last = item;

	wq->qlen++;

	if (wq->idle > 0) {
		status = acl_pthread_cond_signal(&wq->worker_cond);
		if ( status != 0 ) {
			__SET_ERRNO(status);
			acl_msg_error("%s(%d)->%s: pthread_cond_signal, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
			return;
		}
	} else if (wq->counter < wq->parallelism) {
		status = acl_pthread_create(&id,
					&wq->attr,
					__worker_thread,
					(void*) wq);
		if (status != 0) {
			__SET_ERRNO(status);
			acl_msg_fatal("%s(%d)->%s: pthread_create worker, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
		}
		wq->counter++;
	} else if (wq->qlen > 10 * wq->parallelism) {
		time_t now = time(NULL);

		if (now - wq->last_warn >= 2) {
			acl_msg_warn("%s(%d)->%s: reached the max_thread = %d"
				", push into the queue now, qlen=%d, idle=%d",
				__FILE__, __LINE__, myname,
				wq->parallelism, wq->qlen, wq->idle);
			wq->last_warn = now;
		}

		if (wq->overload_timewait > 0) {
			acl_msg_warn("%s(%d), %s: sleep %d seconds",
				__FILE__, __LINE__, myname, wq->overload_timewait);
			sleep(wq->overload_timewait);
		}
	}

	return;
}

int acl_workq_add(ACL_WORK_QUEUE *wq,
			void (*worker_fn)(int, void *),
			int  event_type,
			void *worker_arg)
{
	char  myname[] = "acl_workq_add";
	ACL_WORK_ELEMENT *item;
	int   status;
	char  buf[256];

	if (wq->valid != ACL_WORK_QUEUE_VALID || worker_fn == NULL)
		return (ACL_EINVAL);

	item = (ACL_WORK_ELEMENT*) acl_mymalloc(sizeof(ACL_WORK_ELEMENT));
	if (item == NULL)
		return (ACL_ENOMEM);

	item->worker_fn  = worker_fn;
	item->event_type = event_type;
	item->worker_arg = worker_arg;
	item->next       = NULL;

	status = acl_pthread_mutex_lock(&wq->worker_mutex);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_myfree(item);
		acl_msg_fatal("%s(%d)->%s: pthread_mutex_lock, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
	}

	__workq_addone(wq, item);

	status = acl_pthread_mutex_unlock(&wq->worker_mutex);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_fatal("%s(%d)->%s: pthread_mutex_unlock error=%s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
	}

	return (0);
}

void acl_workq_batadd_begin(void *arg)
{
	char  myname[] = "acl_workq_batadd_begin";
	ACL_WORK_QUEUE *wq = (ACL_WORK_QUEUE *) arg;
	int   status;
	char  buf[256];

	if (wq == NULL)
		acl_msg_fatal("%s(%d)->%s: invalid input",
				__FILE__, __LINE__, myname);

	if (wq->valid != ACL_WORK_QUEUE_VALID)
		acl_msg_fatal("%s(%d)->%s: invalid wq->valid",
				__FILE__, __LINE__, myname);

	status = acl_pthread_mutex_lock(&wq->worker_mutex);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_fatal("%s(%d)->%s: pthread_mutex_lock, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
	}
}

void acl_workq_batadd_one(ACL_WORK_QUEUE *wq,
			void (*worker_fn)(int, void *),
			int  event_type,
			void *worker_arg)
{
	char  myname[] = "acl_workq_batadd_one";
	ACL_WORK_ELEMENT *item;
	char  buf[256];

	if (wq == NULL)
		acl_msg_fatal("%s(%d)->%s: invalid input",
				__FILE__, __LINE__, myname);

	if (wq->valid != ACL_WORK_QUEUE_VALID || worker_fn == NULL)
		acl_msg_fatal("%s(%d)->%s: invalid wq->valid or worker_fn",
				__FILE__, __LINE__, myname);

	item = (ACL_WORK_ELEMENT*) acl_mymalloc(sizeof(ACL_WORK_ELEMENT));
	if (item == NULL) {
		acl_msg_fatal("%s(%d)->%s: can't malloc, error=%s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
	}

	item->worker_fn  = worker_fn;
	item->event_type = event_type;
	item->worker_arg = worker_arg;
	item->next       = NULL;

	__workq_addone(wq, item);
}

void acl_workq_batadd_end(void *arg)
{
	char  myname[] = "acl_workq_batadd_end";
	ACL_WORK_QUEUE *wq = (ACL_WORK_QUEUE *) arg;
	int   status;
	char  buf[256];

	if (wq == NULL)
		acl_msg_fatal("%s(%d)->%s: invalid input",
				__FILE__, __LINE__, myname);
	if (wq->valid != ACL_WORK_QUEUE_VALID)
		acl_msg_fatal("%s(%d)->%s: invalid wq->valid",
				__FILE__, __LINE__, myname);

	status = acl_pthread_mutex_unlock(&wq->worker_mutex);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_fatal("%s(%d)->%s: pthread_mutex_unlock, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
	}
}

int acl_workq_start(ACL_WORK_QUEUE *wq)
{
	char  myname[] = "acl_workq_loop";
	acl_pthread_t id;
	int   status;
	char  buf[256];

	if (wq == NULL || wq->valid != ACL_WORK_QUEUE_VALID) {
		acl_msg_error("%s(%d)->%s: input invalid",
				__FILE__, __LINE__, myname);
		return (-1);
	}

	if (wq->poller_fn == NULL) {
		acl_msg_warn("%s, %s(%d): poller_fn is null, don't need call %s",
			__FILE__, myname, __LINE__, myname);
		return (-1);
	}

	status = acl_pthread_mutex_lock(&wq->poller_mutex);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_error("%s, %s(%d): lock poller_mutex error(%s)",
				__FILE__, myname, __LINE__,
				acl_last_strerror(buf, sizeof(buf)));
		return (-1);
	}

	if (wq->poller_running) {
		acl_msg_error("%s, %s(%d): server is running",
				__FILE__, myname, __LINE__);
		return (-1);
	}

	status = acl_pthread_mutex_unlock(&wq->poller_mutex);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_error("%s, %s(%d): unlock poller_mutex error(%s)",
				__FILE__, myname, __LINE__,
				acl_last_strerror(buf, sizeof(buf)));
		return (-1);
	}

	__init_workq(wq);

	status = acl_pthread_create(&id, &wq->attr, __poller_thread, (void*) wq);
	if (status != 0) {
		__SET_ERRNO(status);
		acl_msg_error("%s(%d)->%s: pthread_create poller, serr = %s",
				__FILE__, __LINE__, myname,
				acl_last_strerror(buf, sizeof(buf)));
		return (status);
	}

	return (0);
}

int acl_workq_batadd_dispatch(void *dispatch_arg,
			void (*worker_fn)(int, void *),
			int  event_type,
			void *worker_arg)
{
	char  myname[] = "acl_workq_batadd_dispatch";
	ACL_WORK_QUEUE *wq;

	if (dispatch_arg == NULL || worker_fn == NULL)
		acl_msg_fatal("%s(%d)->%s: invalid input",
				__FILE__, __LINE__, myname);

	wq = (ACL_WORK_QUEUE *) dispatch_arg;

	acl_workq_batadd_one(wq, worker_fn, event_type, worker_arg);

	return (0);
}
int acl_workq_dispatch(void *dispatch_arg,
			void (*worker_fn)(int, void *),
			int  event_type,
			void *worker_arg)
{
	char  myname[] = "acl_workq_dispatch";
	ACL_WORK_QUEUE *wq;

	if (dispatch_arg == NULL || worker_fn == NULL)
		acl_msg_fatal("%s(%d)->%s: invalid input",
				__FILE__, __LINE__, myname);

	wq = (ACL_WORK_QUEUE *) dispatch_arg;

	return (acl_workq_add(wq, worker_fn, event_type, worker_arg));
}

int acl_workq_quit_wait(ACL_WORK_QUEUE *wq)
{
	if (wq && wq->worker_quit)
		return (1);

	return (0);
}

int acl_workq_nworker(ACL_WORK_QUEUE *wq)
{
	const char *myname = "acl_workq_nworker";
	int   status, n;

	status = acl_pthread_mutex_lock(&wq->worker_mutex);
	if (status) {
		acl_msg_error("%s(%d)->%s: pthread_mutex_lock error(%s)",
			__FILE__, __LINE__, myname, strerror(status));
		return (-1);
	}

	n = wq->counter;

	status = acl_pthread_mutex_unlock(&wq->worker_mutex);
	if (status) {
		acl_msg_error("%s(%d)->%s: pthread_mutex_unlock error(%s)",
			__FILE__, __LINE__, myname, strerror(status));
		return (-1);
	}

	return (n);
}

void acl_workq_set_stacksize(size_t stacksize)
{
	if (stacksize > 0)
		__thread_stacksize = stacksize;
}
