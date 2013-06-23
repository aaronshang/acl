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

void read_notify_callback(int event_type, void *context)
{
	ACL_IOCTL_CTX *ctx;
	ACL_IOCTL *h_ioctl;
	ACL_VSTREAM *h_stream;
	ACL_IOCTL_NOTIFY_FN notify_fn;
	void *arg;

	ctx         = (ACL_IOCTL_CTX *) context;
	h_ioctl     = ctx->h_ioctl;
	h_stream    = ctx->h_stream;
	notify_fn   = ctx->notify_fn;
	arg         = ctx->context;

	acl_event_disable_read(h_ioctl->event, h_stream);

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

void write_notify_callback(int event_type, void *context)
{
	ACL_IOCTL_CTX *ctx;
	ACL_IOCTL *h_ioctl;
	ACL_VSTREAM *h_stream;
	ACL_IOCTL_NOTIFY_FN notify_fn;
	void *arg;

	ctx         = (ACL_IOCTL_CTX *) context;
	h_ioctl     = ctx->h_ioctl;
	h_stream    = ctx->h_stream;
	notify_fn   = ctx->notify_fn;
	arg         = ctx->context;

	acl_event_disable_write(h_ioctl->event, h_stream);

	switch (event_type) {
	case ACL_EVENT_WRITE:
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

void listen_notify_callback(int event_type, void *context)
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
	case ACL_EVENT_RW_TIMEOUT:
	case ACL_EVENT_XCPT:
		acl_event_disable_read(h_ioctl->event, h_stream);
	case ACL_EVENT_READ:
		notify_fn(event_type, h_ioctl, h_stream, arg);
		break;
	default:
		acl_msg_fatal("%s(%d): unknown event type(%d)",
			__FILE__, __LINE__, event_type);
		/* not reached */
		break;
	}
}
