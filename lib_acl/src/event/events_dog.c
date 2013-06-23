#include "StdAfx.h"
#ifndef ACL_PREPARE_COMPILE

#include "stdlib/acl_define.h"
#include <stdlib.h>
#include <stdio.h>

#ifdef ACL_BCB_COMPILER
#pragma hdrstop
#endif

#include "stdlib/acl_mymalloc.h"
#include "stdlib/acl_msg.h"
#include "svr/acl_svr.h"
#include "net/acl_vstream_net.h"
#include "stdlib/acl_iostuff.h"
#include "event/acl_events.h"

#endif

#include "events_dog.h"

struct EVENT_DOG {
	ACL_EVENT *eventp; 
	ACL_VSTREAM *sstream;
	ACL_VSTREAM *server;
	ACL_VSTREAM *client;
	int   thread_mode;
};

/* forward declare */

static void event_dog_reopen(EVENT_DOG *evdog);

static void event_dog_close(EVENT_DOG *evdog)
{
	if (evdog->sstream)
		acl_vstream_close(evdog->sstream);
	if (evdog->server)
		acl_vstream_close(evdog->server);
	if (evdog->client) {
		if (!evdog->thread_mode)
			acl_event_disable_read(evdog->eventp, evdog->client);
		acl_vstream_close(evdog->client);
	}

	evdog->sstream = NULL;
	evdog->server = NULL;
	evdog->client = NULL;
}

static void read_fn(int event_type acl_unused, void *context)
{
	EVENT_DOG *evdog = (EVENT_DOG*) context;
	char  buf[2];

	evdog->client->rw_timeout = 1;
	if (acl_vstream_readn(evdog->client, buf, 1) == ACL_VSTREAM_EOF) {
	        acl_event_disable_read(evdog->eventp, evdog->client);
		event_dog_reopen(evdog);
	} else
		acl_event_enable_read(evdog->eventp, evdog->client, 0, read_fn, evdog);
}

static void event_dog_open(EVENT_DOG *evdog)
{
	const char *myname = "event_dog_open";
	const char *addr = "127.0.0.1:0";
	char  ebuf[256];

	evdog->sstream = acl_vstream_listen(addr, 32);
	if (evdog->sstream == NULL)
		acl_msg_fatal("%s(%d): listen on addr(%s) error(%s)",
			myname, __LINE__, addr,
			acl_last_strerror(ebuf, sizeof(ebuf)));

	evdog->server = acl_vstream_connect(evdog->sstream->local_addr,
			ACL_BLOCKING, 0, 0, 1024);
	if (evdog->server == NULL)
		acl_msg_fatal("%s(%d): connect to addr(%s) error(%s)",
			myname, __LINE__, addr,
			acl_last_strerror(ebuf, sizeof(ebuf)));

	if (acl_vstream_writen(evdog->server, ebuf, 1) == ACL_VSTREAM_EOF)
		acl_msg_fatal("%s(%d): pre write error(%s)",
			myname, __LINE__,
			acl_last_strerror(ebuf, sizeof(ebuf)));

	evdog->client = acl_vstream_accept(evdog->sstream, ebuf, sizeof(ebuf));
	if (evdog->client == NULL)
		acl_msg_fatal("%s(%d): accept error(%s)",
			myname, __LINE__,
			acl_last_strerror(ebuf, sizeof(ebuf)));

	if (acl_vstream_readn(evdog->client, ebuf, 1) == ACL_VSTREAM_EOF)
		acl_msg_fatal("%s(%d): pre read error(%s)",
			myname, __LINE__,
			acl_last_strerror(ebuf, sizeof(ebuf)));

	acl_vstream_close(evdog->sstream);
	evdog->sstream = NULL;

	acl_event_enable_read(evdog->eventp, evdog->client, 0, read_fn, evdog);
}

static void event_dog_reopen(EVENT_DOG *evdog)
{
	event_dog_close(evdog);
	event_dog_open(evdog);
}

EVENT_DOG *event_dog_create(ACL_EVENT *eventp, int thread_mode)
{
	const char *myname = "event_dog_create";
	EVENT_DOG *evdog;

	evdog = (EVENT_DOG*) acl_mycalloc(1, sizeof(EVENT_DOG));
	if (evdog == NULL)
		acl_msg_fatal("%s(%d): calloc error", myname, __LINE__);

	evdog->eventp = eventp;
	evdog->thread_mode = thread_mode;

	event_dog_open(evdog);
	return (evdog);
}

ACL_VSTREAM *event_dog_client(EVENT_DOG *evdog)
{
	if (evdog && evdog->client)
		return (evdog->client);
	return (NULL);
}

void event_dog_notify(EVENT_DOG *evdog)
{
	const char *myname = "event_dog_notify";
	char  buf[2];

	buf[0] = '0';
	buf[1] = 0;
	
	if (acl_vstream_writen(evdog->server, buf, 1) == ACL_VSTREAM_EOF) {
		acl_msg_error("%s(%d): notify error, reset", myname, __LINE__);
		event_dog_reopen(evdog);
	}
}

void event_dog_free(EVENT_DOG *evdog)
{
	event_dog_close(evdog);
	acl_myfree(evdog);
}

