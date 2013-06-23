#include "StdAfx.h"
#ifndef ACL_PREPARE_COMPILE

#include "stdlib/acl_define.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ACL_BCB_COMPILER
#pragma hdrstop
#endif

#include "stdlib/acl_stdlib.h"
#include "event/acl_events.h"
#include "svr/acl_svr.h"
#include "net/acl_net.h"
#include "ioctl/acl_ioctl.h"

#endif

#include "ioctl_internal.h"

static int __poller_fn(void *arg)
{       
	ACL_IOCTL *h_ioctl = (ACL_IOCTL *) arg;

	acl_event_loop(h_ioctl->event);
	return (0);
}                               

ACL_IOCTL *acl_ioctl_create_ex(int event_mode,
				int max_threads,
				int idle_timeout,
				int delay_sec,
				int delay_usec)
{
	const char *myname = "acl_ioctl_create_ex";
	ACL_IOCTL *h_ioctl;

	if (max_threads < 0)
		max_threads = 0;
	if (max_threads > 0 && idle_timeout <= 0) {
		idle_timeout = 60;
		acl_msg_error("%s, %s(%d): idle_timeout(%d) invalid",
			__FILE__, myname, __LINE__, idle_timeout);
	}

	h_ioctl = (ACL_IOCTL *) acl_mycalloc(1, sizeof(ACL_IOCTL));

	if (delay_sec <= 0 && delay_usec <= 0) {
		delay_sec = 1;
		delay_usec = 0;
	}
	h_ioctl->event_mode   = event_mode;
	h_ioctl->max_threads  = max_threads;
	h_ioctl->idle_timeout = idle_timeout;
	h_ioctl->delay_sec    = delay_sec;
	h_ioctl->delay_usec   = delay_usec;
	h_ioctl->stacksize    = 0;

	return (h_ioctl);
}

ACL_IOCTL *acl_ioctl_create(int max_threads, int idle_timeout)
{
	int   delay_sec = 0, delay_usec = 5000;

	return (acl_ioctl_create_ex(ACL_EVENT_SELECT, max_threads,
			idle_timeout, delay_sec, delay_usec));
}

void acl_ioctl_ctl(ACL_IOCTL *h_ioctl, int name, ...)
{
	va_list ap;

	if (h_ioctl == NULL)
		return;

	va_start(ap, name);

	for (; name != ACL_IOCTL_CTL_END; name = va_arg(ap, int)) {
		switch (name) {
		case ACL_IOCTL_CTL_THREAD_MAX:
			h_ioctl->max_threads = va_arg(ap, int);
			break;
		case ACL_IOCTL_CTL_THREAD_STACKSIZE:
			h_ioctl->stacksize = va_arg(ap, int);
			break;
		case ACL_IOCTL_CTL_THREAD_IDLE:
			h_ioctl->idle_timeout = va_arg(ap, int);
			break;
		case ACL_IOCTL_CTL_DELAY_SEC:
			h_ioctl->delay_sec = va_arg(ap, int);
			if (h_ioctl->event)
				acl_event_set_delay_sec(h_ioctl->event, h_ioctl->delay_sec);
			break;
		case ACL_IOCTL_CTL_DELAY_USEC:
			h_ioctl->delay_usec = va_arg(ap, int);
			if (h_ioctl->event)
				acl_event_set_delay_usec(h_ioctl->event, h_ioctl->delay_usec);
			break;
		case ACL_IOCTL_CTL_INIT_FN:
			h_ioctl->thread_init_fn = va_arg(ap, ACL_IOCTL_THREAD_INIT_FN);
			break;
		case ACL_IOCTL_CTL_EXIT_FN:
			h_ioctl->thread_exit_fn = va_arg(ap, ACL_IOCTL_THREAD_EXIT_FN);
			break;
		case ACL_IOCTL_CTL_INIT_CTX:
			h_ioctl->thread_init_arg = va_arg(ap, void*);
			break;
		case ACL_IOCTL_CTL_EXIT_CTX:
			h_ioctl->thread_exit_arg = va_arg(ap, void*);
			break;	
		default:
			acl_msg_fatal("%s(%d): unknown arg", __FILE__, __LINE__);
			/* not reached */
			break;
		}
	}

	va_end(ap);
}

void acl_ioctl_free(ACL_IOCTL *h_ioctl)
{
	if (h_ioctl == NULL)
		return;

	if (h_ioctl->wq)
		acl_workq_destroy(h_ioctl->wq);
	if (h_ioctl->event)
		acl_event_free(h_ioctl->event);

	acl_myfree(h_ioctl);
}

void acl_ioctl_add_dog(ACL_IOCTL *h_ioctl)
{
	if (h_ioctl->max_threads > 0)
		h_ioctl->enable_dog = 1;
	else
		h_ioctl->enable_dog = 0;
}

static int __on_thread_init(void *arg_init, ACL_WORKER_ATTR *attr)
{
	const char *myname = "__on_thread_init";
	ACL_IOCTL *h_ioctl = (ACL_IOCTL*) arg_init;

	if (h_ioctl->thread_init_fn == NULL)
		acl_msg_fatal("%s, %s(%d): thread_init_fn null",
			__FILE__, myname, __LINE__);

	/* bugfix: 2008.8.12, zsx */
	/* if (attr->init_data && attr->init_data != h_ioctl->thread_init_arg) */
	if (attr->init_data && attr->init_data != (void *) h_ioctl)
		acl_msg_fatal("%s, %s(%d): init_data invalid",
			__FILE__, myname, __LINE__);

	/* bugfix: 2008.8.12, zsx */
	/* h_ioctl->thread_init_fn(attr->init_data); */
	h_ioctl->thread_init_fn(h_ioctl->thread_init_arg);
	return (0);
}

static void __on_thread_exit(void *arg_free, ACL_WORKER_ATTR *attr)
{
	const char *myname = "__on_thread_exit";
	ACL_IOCTL *h_ioctl = (ACL_IOCTL*) arg_free;

	if (h_ioctl->thread_exit_fn == NULL)
		acl_msg_fatal("%s, %s(%d): thread_exit_fn null",
			__FILE__, myname, __LINE__);

	/* bugfix: 2008.8.12, zsx */
	/* if (attr->free_data && attr->free_data != h_ioctl->thread_exit_arg) */
	if (attr->free_data && attr->free_data != (void *) h_ioctl)
		acl_msg_fatal("%s, %s(%d): init_data invalid",
			__FILE__, myname, __LINE__);

	/* bugfix: 2008.8.12, zsx */
	/* h_ioctl->thread_exit_fn(attr->free_data); */
	h_ioctl->thread_exit_fn(h_ioctl->thread_exit_arg);
}

int acl_ioctl_start(ACL_IOCTL *h_ioctl)
{
	const char *myname = "acl_ioctl_start";

	if (h_ioctl == NULL)
		acl_msg_fatal("%s, %s(%d): invalid input", __FILE__, myname, __LINE__);

	/* 单线程模式 */
	if (h_ioctl->max_threads == 0) {
		h_ioctl->wq = NULL;
		h_ioctl->event = acl_event_new(h_ioctl->event_mode, 0,
				h_ioctl->delay_sec, h_ioctl->delay_usec);
		return (0);
	}

	/* 多线程模式 */
	h_ioctl->wq = acl_workq_create(h_ioctl->max_threads,
					h_ioctl->idle_timeout,
					__poller_fn,
					(void *) h_ioctl);
	if (h_ioctl->stacksize > 0)
		acl_workq_set_stacksize(h_ioctl->stacksize);
	if (h_ioctl->thread_init_fn)
		(void) acl_workq_atinit(h_ioctl->wq, __on_thread_init, h_ioctl);
	if (h_ioctl->thread_exit_fn)
		(void) acl_workq_atfree(h_ioctl->wq, __on_thread_exit, h_ioctl);

	h_ioctl->event = acl_event_new(h_ioctl->event_mode, 1,
			h_ioctl->delay_sec, h_ioctl->delay_usec);
	if (h_ioctl->enable_dog)
		acl_event_add_dog(h_ioctl->event);

	return (acl_workq_start(h_ioctl->wq));
}

void acl_ioctl_loop(ACL_IOCTL *h_ioctl)
{
	if (h_ioctl && h_ioctl->event)
		acl_event_loop(h_ioctl->event);
}

ACL_EVENT *acl_ioctl_event(ACL_IOCTL *h_ioctl)
{
	if (h_ioctl)
		return (h_ioctl->event);
	return (NULL);
}

void acl_ioctl_disable_readwrite(ACL_IOCTL *h_ioctl, ACL_VSTREAM *h_stream)
{
	if (h_ioctl && h_ioctl->event && h_stream)
		acl_event_disable_readwrite(h_ioctl->event, h_stream);
}

void acl_ioctl_disable_read(ACL_IOCTL *h_ioctl, ACL_VSTREAM *h_stream)
{
	if (h_ioctl && h_ioctl->event && h_stream)
		acl_event_disable_read(h_ioctl->event, h_stream);
}

void acl_ioctl_disable_write(ACL_IOCTL *h_ioctl, ACL_VSTREAM *h_stream)
{
	if (h_ioctl && h_ioctl->event && h_stream)
		acl_event_disable_write(h_ioctl->event, h_stream);
}

int acl_ioctl_isset(ACL_IOCTL *h_ioctl, ACL_VSTREAM *h_stream)
{
	return (acl_event_isset(h_ioctl->event, h_stream));
}

int acl_ioctl_isrset(ACL_IOCTL *h_ioctl, ACL_VSTREAM *h_stream)
{
	return (acl_event_isrset(h_ioctl->event, h_stream));
}

int acl_ioctl_iswset(ACL_IOCTL *h_ioctl, ACL_VSTREAM *h_stream)
{
	return (acl_event_iswset(h_ioctl->event, h_stream));
}

static void __free_ctx(ACL_VSTREAM *stream acl_unused, void *ctx)
{
	acl_myfree(ctx);
}

void acl_ioctl_enable_read(ACL_IOCTL *h_ioctl,
			ACL_VSTREAM *h_stream,
			int timeout,
			ACL_IOCTL_NOTIFY_FN notify_fn,
			void *context)
{
	const char *myname = "acl_ioctl_enable_read";
	ACL_IOCTL_CTX *ctx;

	if (h_ioctl == NULL || h_stream == NULL)
		acl_msg_fatal("%s(%d): input invalid", myname, __LINE__);
	
	if (h_stream->ioctl_read_ctx == NULL) {
		h_stream->ioctl_read_ctx = acl_mymalloc(sizeof(ACL_IOCTL_CTX));
		((ACL_IOCTL_CTX *) h_stream->ioctl_read_ctx)->h_stream = h_stream;
		acl_vstream_add_close_handle(h_stream, __free_ctx, h_stream->ioctl_read_ctx);
	}

	ctx = h_stream->ioctl_read_ctx;

	ctx->h_ioctl   = h_ioctl;
	ctx->notify_fn = notify_fn;
	ctx->context   = context;

	/* 将数据流的状态置入事件监控集合中 */
	if (h_ioctl->max_threads == 0)
		acl_event_enable_read(h_ioctl->event,
					h_stream,
					timeout,
					read_notify_callback,
					(void *) ctx);
	else
		acl_event_enable_read(h_ioctl->event,
					h_stream,
					timeout,
					read_notify_callback_r,
					(void *) ctx);
}

void acl_ioctl_enable_write(ACL_IOCTL *h_ioctl,
			ACL_VSTREAM *h_stream,
			int timeout,
			ACL_IOCTL_NOTIFY_FN notify_fn,
			void *context)
{
	const char *myname = "acl_ioctl_enable_write";
	ACL_IOCTL_CTX *ctx;

	if (h_ioctl == NULL || h_stream == NULL)
		acl_msg_fatal("%s(%d): input invalid", myname, __LINE__);
	
	if (h_stream->ioctl_write_ctx == NULL) {
		h_stream->ioctl_write_ctx = acl_mymalloc(sizeof(ACL_IOCTL_CTX));
		((ACL_IOCTL_CTX *) h_stream->ioctl_write_ctx)->h_stream = h_stream;
		acl_vstream_add_close_handle(h_stream, __free_ctx, h_stream->ioctl_write_ctx);
	}

	ctx = h_stream->ioctl_write_ctx;

	ctx->h_ioctl   = h_ioctl;
	ctx->notify_fn = notify_fn;
	ctx->context   = context;

	/* 将客户端数据流的状态置入事件监控集合中 */
	if (h_ioctl->max_threads == 0)
		acl_event_enable_write(h_ioctl->event,
					h_stream,
					timeout,
					write_notify_callback,
					(void *) ctx);
	else
		acl_event_enable_write(h_ioctl->event,
					h_stream,
					timeout,
					write_notify_callback_r,
					(void *) ctx);
}

void acl_ioctl_enable_connect(ACL_IOCTL *h_ioctl,
				ACL_VSTREAM *h_stream,
				int timeout,
				ACL_IOCTL_NOTIFY_FN notify_fn,
				void *context)
{
	const char *myname = "acl_ioctl_enable_connect";

	if (h_ioctl == NULL || h_stream == NULL)
		acl_msg_fatal("%s(%d): input invalid", myname, __LINE__);

	acl_ioctl_enable_write(h_ioctl, h_stream, timeout, notify_fn, context);
}

void acl_ioctl_enable_listen(ACL_IOCTL *h_ioctl,
				ACL_VSTREAM *h_stream,
				int timeout,
				ACL_IOCTL_NOTIFY_FN notify_fn,
				void *context)
{
	const char *myname = "acl_ioctl_enable_listen";
	ACL_IOCTL_CTX *ctx;

	if (h_ioctl == NULL || h_stream == NULL)
		acl_msg_fatal("%s(%d): input invalid", myname, __LINE__);

	if (h_stream->ioctl_read_ctx == NULL) {
		h_stream->ioctl_read_ctx = acl_mymalloc(sizeof(ACL_IOCTL_CTX));
		((ACL_IOCTL_CTX *) h_stream->ioctl_read_ctx)->h_stream = h_stream;
		acl_vstream_add_close_handle(h_stream, __free_ctx, h_stream->ioctl_read_ctx);
	}

	ctx = h_stream->ioctl_read_ctx;

	ctx->h_ioctl   = h_ioctl;
	ctx->notify_fn = notify_fn;
	ctx->context   = context;

	if (h_ioctl->max_threads == 0)
		acl_event_enable_listen(h_ioctl->event,
					h_stream,
					timeout,
					listen_notify_callback,
					(void *) ctx);
	else
		acl_event_enable_listen(h_ioctl->event,
					h_stream,
					timeout,
					listen_notify_callback_r,
					(void *) ctx);
}

ACL_VSTREAM *acl_ioctl_connect(const char *addr, int timeout)
{
	ACL_VSTREAM *h_stream;

	if (timeout == 0)
		h_stream = acl_vstream_connect(addr, ACL_NON_BLOCKING, 0, 0, 4096);
	else if (timeout < 0)
		h_stream = acl_vstream_connect(addr, ACL_BLOCKING, 0, 0, 4096);
	else
		h_stream = acl_vstream_connect(addr, ACL_NON_BLOCKING, timeout, 0, 4096);

	return (h_stream);
}

ACL_VSTREAM *acl_ioctl_listen(const char *addr, int qlen)
{
	return (acl_vstream_listen(addr, qlen));
}

ACL_VSTREAM *acl_ioctl_listen_ex(const char *addr, int qlen, int block_mode,
				int io_bufsize, int io_timeout)
{
	return (acl_vstream_listen_ex(addr, qlen, block_mode, io_bufsize, io_timeout));
}

ACL_VSTREAM *acl_ioctl_accept(ACL_VSTREAM *sstream, char *ipbuf, int size)
{
	return (acl_vstream_accept(sstream, ipbuf, size));
}

acl_int64 acl_ioctl_request_timer(ACL_IOCTL *h_ioctl,
				ACL_IOCTL_TIMER_FN timer_fn,
				void *context,
				acl_int64 idle_limit)
{
	const char *myname = "acl_ioctl_request_timer";

	if (h_ioctl == NULL || timer_fn == NULL)
		acl_msg_fatal("%s(%d): input invalid", myname, __LINE__);
	if (h_ioctl->event == NULL)
		acl_msg_fatal("%s(%d): ioctl's event null", myname, __LINE__);

	return (acl_event_request_timer(h_ioctl->event, timer_fn, context, idle_limit, 0));
}

acl_int64 acl_ioctl_cancel_timer(ACL_IOCTL *h_ioctl,
				ACL_IOCTL_TIMER_FN timer_fn,
				void *context)
{
	const char *myname = "acl_ioctl_cancel_timer";

	if (h_ioctl == NULL || h_ioctl->event == NULL || timer_fn == NULL)
		acl_msg_fatal("%s(%d): input invalid", myname, __LINE__);

	return (acl_event_cancel_timer(h_ioctl->event, timer_fn, context));
}
