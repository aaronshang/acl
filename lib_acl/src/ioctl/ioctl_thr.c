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

static void worker_callback_r(int event_type, void *context)
{
	ACL_WORKER_ATTR *worker_attr;
	ACL_IOCTL_CTX *ctx;
	ACL_IOCTL *h_ioctl;
	ACL_VSTREAM *h_stream;
	ACL_IOCTL_NOTIFY_FN notify_fn;
	void *arg;

	worker_attr = (ACL_WORKER_ATTR *) context;
	ctx         = (ACL_IOCTL_CTX *) (worker_attr->run_data);
	h_ioctl     = ctx->h_ioctl;
	h_stream    = ctx->h_stream;
	notify_fn   = ctx->notify_fn;
	arg         = ctx->context;

	notify_fn(event_type, h_ioctl, h_stream, arg);
}

void read_notify_callback_r(int event_type, void *context)
{
	ACL_IOCTL_CTX *ctx;
	ACL_IOCTL *h_ioctl;

	ctx     = (ACL_IOCTL_CTX *) context;
	h_ioctl = ctx->h_ioctl;

	switch (event_type) {
	case ACL_EVENT_READ:
	case ACL_EVENT_RW_TIMEOUT:
	case ACL_EVENT_XCPT:
		acl_workq_add(h_ioctl->wq, worker_callback_r, event_type, ctx);
		break;
	default:
		acl_msg_fatal("%s(%d): unknown event type(%d)",
			__FILE__, __LINE__, event_type);
		/* not reached */
		break;
	}
}

void write_notify_callback_r(int event_type, void *context)
{
	ACL_IOCTL_CTX *ctx;
	ACL_IOCTL *h_ioctl;

	ctx     = (ACL_IOCTL_CTX *) context;
	h_ioctl = ctx->h_ioctl;

	switch (event_type) {
	case ACL_EVENT_WRITE:
	case ACL_EVENT_RW_TIMEOUT:
	case ACL_EVENT_XCPT:
		acl_workq_add(h_ioctl->wq, worker_callback_r, event_type, ctx);
		break;
	default:
		acl_msg_fatal("%s(%d): unknown event type(%d)",
			__FILE__, __LINE__, event_type);
		/* not reached */
		break;
	}
}

void listen_notify_callback_r(int event_type, void *context)
{
	ACL_IOCTL_CTX *ctx;
	ACL_IOCTL *h_ioctl;
	ACL_VSTREAM *h_stream;
	ACL_IOCTL_NOTIFY_FN notify_fn;
	void *arg;

	ctx       = (ACL_IOCTL_CTX *) context;
	h_ioctl   = ctx->h_ioctl;
	h_stream  = ctx->h_stream;
	notify_fn = ctx->notify_fn;
	arg       = ctx->context;

	switch (event_type) {
	case ACL_EVENT_READ:
	case ACL_EVENT_RW_TIMEOUT:
	case ACL_EVENT_XCPT:
		notify_fn(event_type, h_ioctl, h_stream, arg);
		break;
	default:
		acl_msg_fatal("%s(%d): unknown event type(%d)",
			__FILE__, __LINE__, event_type);
		/* not reached */
		break;
	}
}

static void worker_ready_callback(int event_type acl_unused, void *context)
{
	ACL_WORKER_ATTR *worker_attr = (ACL_WORKER_ATTR *) context;
	ACL_IOCTL_CTX *ctx = (ACL_IOCTL_CTX *) (worker_attr->run_data);
	ACL_IOCTL *h_ioctl = ctx->h_ioctl;
	ACL_IOCTL_WORKER_FN callback = ctx->worker_fn;
	void *arg = ctx->context;

	acl_myfree(ctx);
	callback(h_ioctl, arg);
}

int acl_ioctl_add(ACL_IOCTL *h_ioctl, ACL_IOCTL_WORKER_FN callback, void *arg)
{
	char  myname[] = "acl_ioctl_add";
	ACL_IOCTL_CTX *ctx;
	int   ret;

	if (h_ioctl == NULL || h_ioctl->wq == NULL)
		acl_msg_fatal("%s(%d): input invalid", myname, __LINE__);

	ctx = acl_mymalloc(sizeof(ACL_IOCTL_CTX));
	ctx->h_ioctl   = h_ioctl;
	ctx->worker_fn = callback;
	ctx->context   = arg;

	ret = acl_workq_add(h_ioctl->wq, worker_ready_callback, 0, ctx);
	if (ret != 0)
		acl_myfree(ctx);

	return (ret);
}

int acl_ioctl_nworker(ACL_IOCTL *h_ioctl)
{
	return (acl_workq_nworker(h_ioctl->wq));
}
