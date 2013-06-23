#include "StdAfx.h"
#ifndef ACL_PREPARE_COMPILE

#include "stdlib/acl_define.h"

#ifdef  HP_UX
#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED  1
#endif

#include <stdio.h>
#include <stdlib.h>
#ifdef	ACL_UNIX
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#ifdef ACL_BCB_COMPILER
#pragma hdrstop
#endif

#include "net/acl_sane_inet.h"
#include "net/acl_sane_socket.h"

#endif

int acl_getpeername(ACL_SOCKET sockfd, char *buf, size_t size)
{
	struct sockaddr_in sk_addr;
	socklen_t len;
	char  ip[32];
	int   port;

	if (sockfd == ACL_SOCKET_INVALID || buf == NULL || size <= 0)
		return (-1);

	len = sizeof(sk_addr);
	if (getpeername(sockfd,  (struct sockaddr *)&sk_addr, &len) == -1)
		return (-1);

	if (acl_inet_ntoa(sk_addr.sin_addr, ip, sizeof(ip)) == NULL)
		return (-1);
	port = ntohs(sk_addr.sin_port);
	snprintf(buf, size, "%s:%d", ip, port);
	return (0);
}

int acl_getsockname(ACL_SOCKET sockfd, char *buf, size_t size)
{
	struct sockaddr_in sk_addr;
	socklen_t len;
	char  ip[32];
	int   port;

	if (sockfd == ACL_SOCKET_INVALID || buf == NULL || size <= 0)
		return (-1);

	len = sizeof(sk_addr);
	if (getsockname(sockfd,  (struct sockaddr *)&sk_addr, &len) == -1)
		return (-1);
	if (acl_inet_ntoa(sk_addr.sin_addr, ip, sizeof(ip)) == NULL)
		return (-1);
	
	port = ntohs(sk_addr.sin_port);
	snprintf(buf, size, ip, port);
	snprintf(buf, size, "%s:%d", ip, port);
	return (0);
}
