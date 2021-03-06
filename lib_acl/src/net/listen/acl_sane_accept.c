/* System library. */
#include "StdAfx.h"
#ifndef ACL_PREPARE_COMPILE

#include "stdlib/acl_define.h"
#ifdef  HP_UX
#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED  1
#endif

#include <errno.h>
#ifdef ACL_UNIX
#include <sys/socket.h>
#endif

#ifdef ACL_BCB_COMPILER
#pragma hdrstop
#endif

/* Utility library. */

#include "stdlib/acl_msg.h"
#include "net/acl_tcp_ctl.h"
#include "net/acl_listen.h"

#endif

/* acl_sane_accept - sanitize accept() error returns */

ACL_SOCKET acl_sane_accept(ACL_SOCKET sock, struct sockaddr * sa, socklen_t *len)
{
	static int accept_ok_errors[] = {
#ifdef ACL_UNIX
		ACL_EAGAIN,
#endif
		ACL_ECONNREFUSED,
		ACL_ECONNRESET,
		ACL_EHOSTDOWN,
		ACL_EHOSTUNREACH,
		ACL_EINTR,
		ACL_ENETDOWN,
		ACL_ENETUNREACH,
		ACL_ENOTCONN,
		ACL_EWOULDBLOCK,
		ACL_ENOBUFS,			/* HPUX11 */
		ACL_ECONNABORTED,
		0,
	};
	ACL_SOCKET fd;

	/*
	 * XXX Solaris 2.4 accept() returns EPIPE when a UNIX-domain client
	 * has disconnected in the mean time. From then on, UNIX-domain
	 * sockets are hosed beyond recovery. There is no point treating
	 * this as a beneficial error result because the program would go
	 * into a tight loop.
	 * XXX LINUX < 2.1 accept() wakes up before the three-way handshake is
	 * complete, so it can fail with ECONNRESET and other "false alarm"
	 * indications.
	 * 
	 * XXX FreeBSD 4.2-STABLE accept() returns ECONNABORTED when a
	 * UNIX-domain client has disconnected in the mean time. The data
	 * that was sent with connect() write() close() is lost, even though
	 * the write() and close() reported successful completion.
	 * This was fixed shortly before FreeBSD 4.3.
	 * 
	 * XXX HP-UX 11 returns ENOBUFS when the client has disconnected in
	 * the mean time.
	 */
	fd = accept(sock, (struct sockaddr *) sa, (socklen_t *) len);
	if (fd == ACL_SOCKET_INVALID) {
		int  count = 0, err, error = acl_last_error();
		for (; (err = accept_ok_errors[count]) != 0; count++) {
			if (error == err) {
				acl_set_error(ACL_EAGAIN);
				break;
			}
		}
	}

	/*
	 * XXX Solaris select() produces false read events, so that read()
	 * blocks forever on a blocking socket, and fails with EAGAIN on
	 * a non-blocking socket. Turning on keepalives will fix a blocking
	 * socket provided that the kernel's keepalive timer expires before
	 * the Postfix watchdog timer.
	 */
	else if (sa != 0 && sa->sa_family == AF_INET) {
		int     on = 1;

		/* default set client to nodelay --- add by zsx, 2008.9.4 */
		acl_tcp_nodelay(fd, on);

#if defined(BROKEN_READ_SELECT_ON_TCP_SOCKET) && defined(SO_KEEPALIVE)
		(void) setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
			(char *) &on, sizeof(on));
#endif
	}
	return (fd);
}

