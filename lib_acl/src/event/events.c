#include "StdAfx.h"
#ifndef ACL_PREPARE_COMPILE

#include "stdlib/acl_msg.h"

#endif

#include "events.h"

int event_prepare(ACL_EVENT *eventp)
{
	ACL_EVENT_FDTABLE *fdp;
	ACL_SOCKET sockfd;
	int   i, nwait = 0;

	eventp->fdcnt_ready = 0;

	for (i = 0; i < eventp->fdcnt; i++) {
		fdp = eventp->fdtabs[i];
		sockfd = ACL_VSTREAM_SOCK(fdp->stream);
		fdp->event_type = 0;
		if (eventp->maxfd < sockfd)
			eventp->maxfd = sockfd;
		if ((fdp->stream->flag & ACL_VSTREAM_FLAG_BAD) != 0) {
			fdp->stream->flag &= ~ACL_VSTREAM_FLAG_BAD;
			fdp->event_type |= ACL_EVENT_XCPT;
			fdp->fdidx_ready = eventp->fdcnt_ready;
			eventp->fdtabs_ready[eventp->fdcnt_ready++] = fdp;
		} else if ((fdp->flag & EVENT_FDTABLE_FLAG_READ)) {
			if (ACL_VSTREAM_BFRD_CNT(fdp->stream) > 0) {
				fdp->stream->sys_read_ready = 0;
				fdp->event_type |= ACL_EVENT_READ;
				fdp->fdidx_ready = eventp->fdcnt_ready;
				eventp->fdtabs_ready[eventp->fdcnt_ready++] = fdp;
			} else if (fdp->r_ttl > 0 &&  eventp->event_present > fdp->r_ttl) {
				fdp->event_type |= ACL_EVENT_RW_TIMEOUT;
				fdp->fdidx_ready = eventp->fdcnt_ready;
				eventp->fdtabs_ready[eventp->fdcnt_ready++] = fdp;
			} else
				nwait++;
		} else if ((fdp->flag & EVENT_FDTABLE_FLAG_WRITE)) {
			if (fdp->w_ttl > 0 && eventp->event_present > fdp->w_ttl) {
				fdp->event_type |= ACL_EVENT_RW_TIMEOUT;
				fdp->fdidx_ready = eventp->fdcnt_ready;
				eventp->fdtabs_ready[eventp->fdcnt_ready++] = fdp;
			} else
				nwait++;
		} else {
			nwait++;
		}
	}
	return (nwait);
}

void event_fire(ACL_EVENT *eventp)
{
	ACL_EVENT_FDTABLE *fdp;
	int   i, event_type;
	acl_int64   r_timeout, w_timeout;
	ACL_EVENT_NOTIFY_RDWR r_callback, w_callback;

	for (i = 0; i < eventp->fdcnt_ready; i++) {
		fdp = eventp->fdtabs_ready[i];
		/* eventp->fdtabs_ready[i] maybe be set NULL in timer callback */
		if (fdp == NULL || fdp->stream == NULL) 
			continue;
		event_type = fdp->event_type;

		if ((event_type & ACL_EVENT_XCPT) != 0) {
			fdp->event_type &= ~ACL_EVENT_XCPT;
			r_callback = fdp->r_callback;
			w_callback = fdp->w_callback;

			if (r_callback)
				r_callback(ACL_EVENT_XCPT, fdp->r_context);

			/* eventp->fdtabs_ready[i] maybe be set NULL in r_callback */
			if (w_callback && eventp->fdtabs_ready[i])
				w_callback(ACL_EVENT_XCPT, fdp->w_context);
			continue;
		}

		if ((event_type & ACL_EVENT_RW_TIMEOUT) != 0) {
			fdp->event_type &= ~ACL_EVENT_RW_TIMEOUT;
			r_timeout = fdp->r_timeout;
			w_timeout = fdp->w_timeout;
			r_callback = fdp->r_callback;
			w_callback = fdp->w_callback;

			if (r_timeout > 0 && r_callback) {
				fdp->r_ttl = eventp->event_present + fdp->r_timeout;
				fdp->r_callback(ACL_EVENT_RW_TIMEOUT, fdp->r_context);
			}

			/* eventp->fdtabs_ready[i] maybe be set NULL in r_callback */
			if (w_timeout > 0 && w_callback && eventp->fdtabs_ready[i]) {
				fdp->w_ttl = eventp->event_present + fdp->w_timeout;
				fdp->w_callback(ACL_EVENT_RW_TIMEOUT, fdp->w_context);
			}
			continue;
		}

		if ((event_type & ACL_EVENT_READ) != 0) {
			fdp->event_type &= ~ACL_EVENT_READ;
			if (fdp->r_timeout > 0)
				fdp->r_ttl = eventp->event_present + fdp->r_timeout;
			fdp->r_callback(ACL_EVENT_READ, fdp->r_context);
		}

		/* eventp->fdtabs_ready[i] maybe be set NULL in fdp->r_callback() */
		if ((event_type & ACL_EVENT_WRITE) && eventp->fdtabs_ready[i]) {
			if (fdp->w_timeout > 0)
				fdp->w_ttl = eventp->event_present + fdp->w_timeout;
			fdp->event_type &= ~ACL_EVENT_WRITE;
			fdp->w_callback(ACL_EVENT_WRITE, fdp->w_context);
		}
	}
}

int event_thr_prepare(ACL_EVENT *eventp)
{
	ACL_SOCKET sockfd;
	ACL_EVENT_FDTABLE *fdp;
	int   i, nwait = 0;

	eventp->fdcnt_ready = 0;

	for (i = 0; i < eventp->fdcnt; i++) {
		fdp = eventp->fdtabs[i];
		sockfd = ACL_VSTREAM_SOCK(fdp->stream);
		fdp->event_type = 0;
		if (eventp->maxfd < sockfd)
			eventp->maxfd = sockfd;

		if (fdp->listener) {
			nwait++;
			continue;
		}

		if (fdp->stream->flag & ACL_VSTREAM_FLAG_BAD) {
			fdp->stream->flag &= ~ACL_VSTREAM_FLAG_BAD;
			fdp->event_type |= ACL_EVENT_XCPT;
			fdp->fdidx_ready = eventp->fdcnt_ready;
			eventp->fdtabs_ready[eventp->fdcnt_ready++] = fdp;
		} else if ((fdp->flag & EVENT_FDTABLE_FLAG_READ)) {
			if (ACL_VSTREAM_BFRD_CNT(fdp->stream) > 0) {
				fdp->stream->sys_read_ready = 0;
				fdp->event_type = ACL_EVENT_READ;
				fdp->fdidx_ready = eventp->fdcnt_ready;
				eventp->fdtabs_ready[eventp->fdcnt_ready++] = fdp;
			} else if (fdp->r_ttl > 0 && eventp->event_present > fdp->r_ttl) {
				fdp->event_type = ACL_EVENT_RW_TIMEOUT;
				fdp->fdidx_ready = eventp->fdcnt_ready;
				eventp->fdtabs_ready[eventp->fdcnt_ready++] = fdp;
			} else
				nwait++;
		} else if ((fdp->flag & EVENT_FDTABLE_FLAG_WRITE)) {
			if (fdp->w_ttl > 0 && eventp->event_present > fdp->w_ttl) {
				fdp->event_type = ACL_EVENT_RW_TIMEOUT;
				fdp->fdidx_ready = eventp->fdcnt_ready;
				eventp->fdtabs_ready[eventp->fdcnt_ready++] = fdp;
			} else
				nwait++;
		} else {
			nwait++;
		}
	}

	return (nwait);
}

void event_thr_fire(ACL_EVENT *eventp)
{
	ACL_EVENT_FDTABLE *fdp;
	ACL_EVENT_NOTIFY_FN worker_fn;
	void *worker_arg;
	int   i;

	for (i = 0; i < eventp->fdcnt_ready; i++) {
		fdp = eventp->fdtabs_ready[i];
		/* eventp->fdtabs_ready[i] maybe be set NULL by timer callback */
		if (fdp == NULL || fdp->stream == NULL)
			continue;

		if (fdp->event_type & ACL_EVENT_XCPT) {
			fdp->event_type &= ~ACL_EVENT_XCPT;
			if (fdp->r_callback) {
				worker_fn = fdp->r_callback;
				worker_arg = fdp->r_context;
			} else if (fdp->w_callback) {
				worker_fn = fdp->w_callback;
				worker_arg = fdp->w_context;
			} else {
				worker_fn = NULL;
				worker_arg = NULL;
			}
			eventp->disable_readwrite_fn(eventp, fdp->stream);
			if (worker_fn)
				worker_fn(ACL_EVENT_XCPT, worker_arg);
		} else if (fdp->event_type & ACL_EVENT_RW_TIMEOUT) {
			fdp->event_type &= ~ACL_EVENT_RW_TIMEOUT;
			if (fdp->r_callback) {
				worker_fn = fdp->r_callback;
				worker_arg = fdp->r_context;
			} else if (fdp->w_callback) {
				worker_fn = fdp->w_callback;
				worker_arg = fdp->w_context;
			} else {
				worker_fn = NULL;
				worker_arg = NULL;
			}
			if (!fdp->listener)
				eventp->disable_readwrite_fn(eventp, fdp->stream);
			if (worker_fn)
				worker_fn(ACL_EVENT_RW_TIMEOUT, worker_arg);
		} else if (fdp->event_type & ACL_EVENT_READ) {
			fdp->event_type &= ~ACL_EVENT_READ;
			worker_fn = fdp->r_callback;
			worker_arg = fdp->r_context;
			if (!fdp->listener)
				eventp->disable_readwrite_fn(eventp, fdp->stream);
			worker_fn(ACL_EVENT_READ, worker_arg);
		} else if (fdp->event_type & ACL_EVENT_WRITE) {
			fdp->event_type &= ~ACL_EVENT_WRITE;
			worker_fn = fdp->w_callback;
			worker_arg = fdp->w_context;
			eventp->disable_readwrite_fn(eventp, fdp->stream);
			worker_fn(ACL_EVENT_WRITE, worker_arg);
		}
	}
}
