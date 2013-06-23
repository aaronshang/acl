/* System library. */
#include "StdAfx.h"
#ifndef ACL_PREPARE_COMPILE

#include "stdlib/acl_define.h"

#ifdef ACL_BCB_COMPILER
#pragma hdrstop
#endif

#endif

#ifdef ACL_UNIX

#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#ifdef STRCASECMP_IN_STRINGS_H
#include <strings.h>
#endif
#include <time.h>
#include <pthread.h>

/* Utility library. */

#include "stdlib/acl_msg.h"
#include "stdlib/unix/acl_chroot_uid.h"
#include "stdlib/acl_vstring.h"
#include "stdlib/acl_vstream.h"
#include "stdlib/acl_mymalloc.h"
#include "stdlib/acl_iostuff.h"
#include "stdlib/acl_stringops.h"
#include "stdlib/acl_myflock.h"
#include "stdlib/unix/acl_watchdog.h"
#include "stdlib/acl_split_at.h"
#include "net/acl_listen.h"
#include "event/acl_events.h"

/* Global library. */

#include "../master_flow.h"
#include "../master_params.h"
#include "../master_proto.h"

/* Application-specific */
#include "master/acl_listener_params.h"
#include "master/acl_server_api.h"

int   acl_var_listener_pid;
char *acl_var_listener_procname;
char *acl_var_listener_log_file;

int   acl_var_listener_buf_size;
int   acl_var_listener_rw_timeout;
int   acl_var_listener_in_flow_delay;
int   acl_var_listener_idle_limit;
char *acl_var_listener_queue_dir;
char *acl_var_listener_owner;
int   acl_var_listener_delay_sec;
int   acl_var_listener_delay_usec;
int   acl_var_listener_daemon_timeout;
int   acl_var_listener_use_limit;
char *acl_var_listener_pid_dir;

static ACL_CONFIG_INT_TABLE __conf_int_tab[] = {
        { ACL_VAR_LISTENER_BUF_SIZE, ACL_DEF_LISTENER_BUF_SIZE, &acl_var_listener_buf_size, 0, 0 },
        { ACL_VAR_LISTENER_RW_TIMEOUT, ACL_DEF_LISTENER_RW_TIMEOUT, &acl_var_listener_rw_timeout, 0, 0 },
        { ACL_VAR_LISTENER_IN_FLOW_DELAY, ACL_DEF_LISTENER_IN_FLOW_DELAY, &acl_var_listener_in_flow_delay, 0, 0 },
        { ACL_VAR_LISTENER_IDLE_LIMIT, ACL_DEF_LISTENER_IDLE_LIMIT, &acl_var_listener_idle_limit, 0, 0 },
        { ACL_VAR_LISTENER_DELAY_SEC, ACL_DEF_LISTENER_DELAY_SEC, &acl_var_listener_delay_sec, 0, 0 },
        { ACL_VAR_LISTENER_DELAY_USEC, ACL_DEF_LISTENER_DELAY_USEC, &acl_var_listener_delay_usec, 0, 0 },
        { ACL_VAR_LISTENER_DAEMON_TIMEOUT, ACL_DEF_LISTENER_DAEMON_TIMEOUT, &acl_var_listener_daemon_timeout, 0, 0 },
        { ACL_VAR_LISTENER_USE_LIMIT, ACL_DEF_LISTENER_USE_LIMIT, &acl_var_listener_use_limit, 0, 0 },
        { 0, 0, 0, 0, 0 },
};

static ACL_CONFIG_STR_TABLE __conf_str_tab[] = {
        { ACL_VAR_LISTENER_QUEUE_DIR, ACL_DEF_LISTENER_QUEUE_DIR, &acl_var_listener_queue_dir },
        { ACL_VAR_LISTENER_OWNER, ACL_DEF_LISTENER_OWNER, &acl_var_listener_owner },
	{ ACL_VAR_LISTENER_PID_DIR, ACL_DEF_LISTENER_PID_DIR, &acl_var_listener_pid_dir },
        { 0, 0, 0 },
};

 /*
  * Global state.
  */
static int client_count = 0;
static int use_count = 0;
static int socket_count = 1;

static pthread_mutex_t	__counter_mutex;

static time_t __last_closed_time = 0;
static pthread_mutex_t	__closed_time_mutex;

static ACL_EVENT *__eventp = NULL;
static ACL_VSTREAM **__stream_array;

static void (*listener_server_service) (ACL_VSTREAM *, char *, char **);
static char *listener_server_name;
static char **listener_server_argv;
static void (*listener_server_accept) (int, void *);
static void (*listener_server_onexit) (char *, char **);
static void (*listener_server_pre_accept) (char *, char **);
static ACL_VSTREAM *listener_server_lock;
static int listener_server_in_flow_delay;
static unsigned listener_server_generation;
static void (*listener_server_pre_disconn) (ACL_VSTREAM *, char *, char **);
static int (*listener_server_on_accept)(ACL_VSTREAM *);

/* forward declare */
static void listener_server_timeout(int unused_event, void *unused_context);

static void __listener_init(void)
{
	pthread_mutex_init(&__counter_mutex, NULL);
	pthread_mutex_init(&__closed_time_mutex, NULL);
	__last_closed_time = time(NULL);
}

static void __lock_counter(void)
{
	pthread_mutex_lock(&__counter_mutex);
}

static void __unlock_counter(void)
{
	pthread_mutex_unlock(&__counter_mutex);
}

static void __lock_closed_time(void)
{
	pthread_mutex_lock(&__closed_time_mutex);
}

static void __unlock_closed_time(void)
{
	pthread_mutex_unlock(&__closed_time_mutex);
}

/* add by zsx for rw timeout, 2005.9.25*/
void acl_listener_server_request_rw_timer(ACL_VSTREAM *stream)
{
	char  myname[] = "acl_listener_server_request_rw_timer";

	if (stream == NULL)
		acl_msg_fatal("%s(%d)->%s: input error",
				__FILE__, __LINE__, myname);
	if (__eventp == NULL)
		acl_msg_fatal("%s(%d)->%s: event has not been inited",
				__FILE__, __LINE__, myname);
}

/* add by zsx for rw timeout, 2005.9.25*/
void acl_listener_server_cancel_rw_timer(ACL_VSTREAM *stream)
{
	char  myname[] = "acl_listener_server_cancel_rw_timer";

	if (stream == NULL)
		acl_msg_fatal("%s(%d)->%s: input error",
				__FILE__, __LINE__, myname);
	if (__eventp == NULL)
		acl_msg_fatal("%s(%d)->%s: event has not been inited",
				__FILE__, __LINE__, myname);
}

/* listener_server_exit - normal termination */

static void listener_server_exit(void)
{
	if (listener_server_onexit)
		listener_server_onexit(listener_server_name, listener_server_argv);
	exit(0);
}

/* listener_server_abort - terminate after abnormal master exit */

static void listener_server_abort(int unused_event, void *unused_context)
{
	unused_event = unused_event;
	unused_context = unused_context;

	if (acl_msg_verbose)
		acl_msg_info("master disconnect -- exiting");
	listener_server_exit();
}

static void __set_idle_timer(int time_left)
{
	if (time_left <= 0)
		time_left = acl_var_listener_idle_limit;

	acl_event_request_timer(__eventp, listener_server_timeout,
		(void *) 0, (acl_int64) time_left * 1000000, 0);
}

static int __clr_idle_timer(void)
{
	int   time_left;

	time_left = (int) ((acl_event_cancel_timer(__eventp,
		listener_server_timeout, (void *) 0) + 999999) / 1000000);

	return (time_left);
}

static void __update_close_time(void)
{
	__lock_closed_time();
	__last_closed_time = time(NULL);
	__unlock_closed_time();
}

static time_t __get_close_time(void)
{
	time_t   n;

	__lock_closed_time();
	n = __last_closed_time; 
	__unlock_closed_time();

	return (n);
}

/* listener_server_timeout - idle time exceeded */

static void listener_server_timeout(int unused_event, void *unused_context)
{
	time_t last, inter;

	unused_event = unused_event;
	unused_context = unused_context;

	/* if there are some fds not be closed, the timer should be reset again */
	if (client_count > 0 && acl_var_listener_idle_limit > 0) {
		__set_idle_timer(acl_var_listener_idle_limit);
		return;
	}

	last  = __get_close_time();
	inter = time(NULL) - last;

	if (inter >= 0 && inter < acl_var_listener_idle_limit) {
		__set_idle_timer(acl_var_listener_idle_limit - inter);
		return;
	}

	if (acl_msg_verbose)
		acl_msg_info("idle timeout -- exiting");

	listener_server_exit();
}

static void __increase_client_counter(void)
{
	__lock_counter();
	client_count++;
	__unlock_counter();
}

static void __decrease_client_counter(void)
{
	char  myname[] = "__decrease_client_counter";

	__lock_counter();
	client_count--;
	if (client_count < 0) {
		acl_msg_error("%s(%d): client_count = %d < 0", myname, __LINE__, client_count);
		client_count = 0;
	}
	__unlock_counter();
}

/*  acl_listener_server_drain - stop accepting new clients */

int acl_listener_server_drain(void)
{
	int     fd;
	ACL_VSTREAM *stream = NULL;

	switch (fork()) {
		/* Try again later. */
	case -1:
		return (-1);
		/* Finish existing clients in the background, then terminate. */
	case 0:
		for (fd =ACL_MASTER_LISTEN_FD;
		     fd < ACL_MASTER_LISTEN_FD + socket_count;
		     fd++) {
			stream = __stream_array[fd];
			acl_event_disable_readwrite(__eventp, stream);
		}
		acl_var_listener_use_limit = 1;
		return (0);
		/* Let the master start a new process. */
	default:
		exit(0);
	}
}


/* acl_listener_server_disconnect - terminate client session */

void acl_listener_server_disconnect(ACL_VSTREAM *stream)
{
	if (acl_msg_verbose)
		acl_msg_info("connection closed fd %d", ACL_VSTREAM_SOCK(stream));
	if (listener_server_pre_disconn)
		listener_server_pre_disconn(stream, listener_server_name, listener_server_argv);
	(void) acl_vstream_fclose(stream);
}

/* listener_server_execute - in case (char *) != (struct *) */

static void listener_server_execute(int event, ACL_VSTREAM *stream)
{
	char  myname[] = "listener_server_execute";

	event = event;

	if (listener_server_lock != 0
	    && acl_myflock(ACL_VSTREAM_FILE(listener_server_lock),
		    	ACL_INTERNAL_LOCK,
			ACL_MYFLOCK_OP_NONE) < 0)
		acl_msg_fatal("%s(%d)->%s: select unlock: %s",
				__FILE__, __LINE__, myname, strerror(errno));

	if (acl_master_notify(acl_var_listener_pid, listener_server_generation, ACL_MASTER_STAT_TAKEN) < 0)
		listener_server_abort(ACL_EVENT_NULL_TYPE, ACL_EVENT_NULL_CONTEXT);

	listener_server_service(stream, listener_server_name, listener_server_argv);

	if (acl_master_notify(acl_var_listener_pid, listener_server_generation, ACL_MASTER_STAT_AVAIL) < 0)
		listener_server_abort(ACL_EVENT_NULL_TYPE, ACL_EVENT_NULL_CONTEXT);

	if (acl_var_listener_idle_limit > 0)
		__set_idle_timer(acl_var_listener_idle_limit);
}

static void __decrease_counter_on_close_stream_fn(ACL_VSTREAM *stream acl_unused, void *arg acl_unused)
{
	__update_close_time();
	__decrease_client_counter();
}

/* listener_server_wakeup - wake up application */

static void listener_server_wakeup(int fd)
{
	ACL_VSTREAM *stream;

	if (acl_msg_verbose)
		acl_msg_info("connection established fd %d", fd);
	acl_non_blocking(fd, ACL_BLOCKING);
	acl_close_on_exec(fd, ACL_CLOSE_ON_EXEC);

	__increase_client_counter();

	use_count++;

	stream = acl_vstream_fdopen(fd,
				O_RDWR,
				acl_var_listener_buf_size,
				acl_var_listener_rw_timeout,
				ACL_VSTREAM_TYPE_SOCK);

	/* when the stream is closed, the callback will be called to decrease the counter */
	acl_vstream_add_close_handle(stream, __decrease_counter_on_close_stream_fn, NULL);

	listener_server_execute(0, stream);
}

/* listener_server_accept_local - accept client connection request */

static void listener_server_accept_local(int unused_event, void *context)
{
	ACL_VSTREAM *stream = (ACL_VSTREAM *) context;
	int     listener_fd = ACL_VSTREAM_SOCK(stream);
	int     time_left = -1;
	int     fd;

	unused_event = unused_event;

	/*
	 * Be prepared for accept() to fail because some other process already
	 * got the connection (the number of processes competing for clients
	 * is kept small, so this is not a "thundering herd" problem). If the
	 * accept() succeeds, be sure to disable non-blocking I/O, in order to
	 * minimize confusion.
	 */
	if (acl_var_listener_idle_limit > 0)
		time_left = __clr_idle_timer();

	if (listener_server_pre_accept)
		listener_server_pre_accept(listener_server_name,
					listener_server_argv);
	fd = acl_unix_accept(listener_fd);
	if (listener_server_lock != 0
	    && acl_myflock(ACL_VSTREAM_FILE(listener_server_lock),
		    	ACL_INTERNAL_LOCK,
			ACL_MYFLOCK_OP_NONE) < 0)
		acl_msg_fatal("select unlock: %s", strerror(errno));
	if (fd < 0) {
		if (errno != EAGAIN)
			acl_msg_fatal("accept connection: %s", strerror(errno));

		if (time_left > 0)
			__set_idle_timer(time_left);
	} else
		listener_server_wakeup(fd);
}

#ifdef MASTER_XPORT_NAME_PASS

/* listener_server_accept_pass - accept descriptor */

static void listener_server_accept_pass(int unused_event, void *context)
{
	ACL_VSTREAM *stream = (ACL_VSTREAM *) context;
	int     listener_fd = acl_vstream_fileno(stream);
	int     time_left = -1;
	int     fd;

	unused_event = unused_event;

	/*
	 * Be prepared for accept() to fail because some other process already
	 * got the connection (the number of processes competing for clients
	 * is kept small, so this is not a "thundering herd" problem). If the
	 * accept() succeeds, be sure to disable non-blocking I/O, in order to
	 * minimize confusion.
	 */
	if (acl_var_listener_idle_limit > 0)
		time_left = __clr_idle_timer();

	if (listener_server_pre_accept)
		listener_server_pre_accept(listener_server_name, listener_server_argv);
	fd = PASS_ACCEPT(listener_fd);
	if (listener_server_lock != 0
	    && acl_myflock(ACL_VSTREAM_FILE(listener_server_lock),
		    	ACL_INTERNAL_LOCK,
			ACL_MYFLOCK_OP_NONE) < 0)
		acl_msg_fatal("select unlock: %s", strerror(errno));
	if (fd < 0) {
		if (errno != EAGAIN)
			acl_msg_fatal("accept connection: %s", strerror(errno));

		if (time_left > 0)
			__set_idle_timer(time_left);
	} else
		listener_server_wakeup(fd);
}

#endif

/* listener_server_accept_inet - accept client connection request */

static void listener_server_accept_inet(int unused_event, void *context)
{
	ACL_VSTREAM	*stream = (ACL_VSTREAM *) context;
	int     listener_fd = ACL_VSTREAM_SOCK(stream);
	int     time_left = -1;
	int     fd;

	unused_event = unused_event;

	/*
	 * Be prepared for accept() to fail because some other process already
	 * got the connection (the number of processes competing for clients
	 * is kept small, so this is not a "thundering herd" problem). If the
	 * accept() succeeds, be sure to disable non-blocking I/O, in order to
	 * minimize confusion.
	 */
	if (acl_var_listener_idle_limit > 0)
		time_left = __clr_idle_timer();

	if (listener_server_pre_accept)
		listener_server_pre_accept(listener_server_name, listener_server_argv);
	fd = acl_inet_accept(listener_fd);
	if (listener_server_lock != 0
	    && acl_myflock(ACL_VSTREAM_FILE(listener_server_lock),
		  	  	ACL_INTERNAL_LOCK,
				ACL_MYFLOCK_OP_NONE) < 0)
		acl_msg_fatal("select unlock: %s", strerror(errno));
	if (fd < 0) {
		if (errno != EAGAIN)
			acl_msg_fatal("accept connection: %s", strerror(errno));

		if (time_left > 0)
			__set_idle_timer(time_left);
	} else
		listener_server_wakeup(fd);
}

/* listener_server_main - the real main program */

void acl_listener_server_main(int argc, char **argv, ACL_LISTEN_SERVER_FN service,...)
{
	char   myname[] = "acl_listener_server_main";
	ACL_VSTREAM *stream = 0;
	char   *root_dir = 0;
	char   *user_name = 0;
	int     debug_me = 0;
	char   *service_name = acl_mystrdup(acl_safe_basename(argv[0]));
	int     c;
	va_list ap;
	ACL_MASTER_SERVER_INIT_FN pre_init = 0;
	ACL_MASTER_SERVER_INIT_FN post_init = 0;
	ACL_MASTER_SERVER_LOOP_FN loop = 0;
	int     key;
	char   *transport = 0;
	int     alone = 0;
	int     zerolimit = 0;
	ACL_WATCHDOG *watchdog;
	char   *generation;
	int     fd, fdtype = 0;

	int     f_flag = 0;
	char   *conf_file_ptr = 0;
	/*
	 * Don't die when a process goes away unexpectedly.
	 */
	signal(SIGPIPE, SIG_IGN);

	/*
	 * Don't die for frivolous reasons.
	 */
#ifdef SIGXFSZ
	signal(SIGXFSZ, SIG_IGN);
#endif

	/*
	 * May need this every now and then.
	 */
	acl_var_listener_pid = getpid();

	acl_var_listener_procname = acl_mystrdup(acl_safe_basename(argv[0]));
	acl_var_listener_log_file = getenv("MASTER_LOG");
	if (acl_var_listener_log_file == NULL)
		acl_msg_fatal("%s(%d)->%s: can't get MASTER_LOG's env value",
				__FILE__, __LINE__, myname);

	/*
	 * Initialize logging and exit handler. Do the syslog first, so that its
	 * initialization completes before we enter the optional chroot jail.
	 */
	acl_msg_open(acl_var_listener_log_file, acl_var_listener_procname);

	acl_get_app_conf_int_table(__conf_int_tab);
	acl_get_app_conf_str_table(__conf_str_tab);

	acl_master_vars_init(acl_var_listener_buf_size, acl_var_listener_rw_timeout);

	if (acl_msg_verbose)
		acl_msg_info("%s(%d): daemon started, log = %s",
				acl_var_listener_procname, __LINE__, acl_var_listener_log_file);

	/*
	 * Pick up policy settings from master process. Shut up error messages to
	 * stderr, because no-one is going to see them.
	 */
	opterr = 0;
	while ((c = getopt(argc, argv, "cdi:lm:n:o:s:it:uvzf:")) > 0) {
		switch (c) {
		case 'f':
			acl_app_conf_load(optarg);
			f_flag = 1;
			conf_file_ptr = optarg;
			break;
		case 'c':
			root_dir = "setme";
			break;
		case 'd':
			debug_me = 1;
			break;
		case 'l':
			alone = 1;
			break;
		case 'n':
			service_name = optarg;
			break;
		case 's':
			if ((socket_count = atoi(optarg)) <= 0)
				acl_msg_fatal("invalid socket_count: %s", optarg);
			break;
		case 'i':
			stream = ACL_VSTREAM_IN;
			break;
		case 'u':
			user_name = "setme";
			break;
		case 't':
			transport = optarg;
			break;
		case 'v':
			acl_msg_verbose++;
			break;
		case 'z':
			zerolimit = 1;
			break;
		default:
#if 0
			acl_msg_warn("%s(%d)->%s: invalid option: %c",
					__FILE__, __LINE__, myname, c);
#endif
			break;
		}
	}

	if (f_flag == 0)
		acl_msg_fatal("%s(%d)->%s: need \"-f pathname\"",
				__FILE__, __LINE__, myname);
	else if (acl_msg_verbose)
		acl_msg_info("%s(%d)->%s: configure file = %s", 
				__FILE__, __LINE__, myname, conf_file_ptr);

	/*
	 * Application-specific initialization.
	 */
	va_start(ap, service);
	while ((key = va_arg(ap, int)) != 0) {
		switch (key) {
		case ACL_MASTER_SERVER_INT_TABLE:
			acl_get_app_conf_int_table(va_arg(ap, ACL_CONFIG_INT_TABLE *));
			break;
		case ACL_MASTER_SERVER_INT64_TABLE:
			acl_get_app_conf_int64_table(va_arg(ap, ACL_CONFIG_INT64_TABLE *));
			break;
		case ACL_MASTER_SERVER_STR_TABLE:
			acl_get_app_conf_str_table(va_arg(ap, ACL_CONFIG_STR_TABLE *));
			break;
		case ACL_MASTER_SERVER_BOOL_TABLE:
			acl_get_app_conf_bool_table(va_arg(ap, ACL_CONFIG_BOOL_TABLE *));
			break;

		case ACL_MASTER_SERVER_PRE_INIT:
			pre_init = va_arg(ap, ACL_MASTER_SERVER_INIT_FN);
			break;
		case ACL_MASTER_SERVER_POST_INIT:
			post_init = va_arg(ap, ACL_MASTER_SERVER_INIT_FN);
			break;
		case ACL_MASTER_SERVER_LOOP:
			loop = va_arg(ap, ACL_MASTER_SERVER_LOOP_FN);
			break;
		case ACL_MASTER_SERVER_EXIT:
			listener_server_onexit = va_arg(ap, ACL_MASTER_SERVER_EXIT_FN);
			break;
		case ACL_MASTER_SERVER_PRE_ACCEPT:
			listener_server_pre_accept = va_arg(ap, ACL_MASTER_SERVER_ACCEPT_FN);
			break;
		case ACL_MASTER_SERVER_PRE_DISCONN:
			listener_server_pre_disconn = va_arg(ap, ACL_MASTER_SERVER_DISCONN_FN);
			break;
		case ACL_MASTER_SERVER_ON_ACCEPT:
			listener_server_on_accept = va_arg(ap, ACL_MASTER_SERVER_ON_ACCEPT_FN);
			break;

		case ACL_MASTER_SERVER_IN_FLOW_DELAY:
			listener_server_in_flow_delay = 1;
			break;
		case ACL_MASTER_SERVER_SOLITARY:
			if (!alone)
				acl_msg_fatal("service %s requires a process limit of 1",
						service_name);
			break;
		case ACL_MASTER_SERVER_UNLIMITED:
			if (!zerolimit)
				acl_msg_fatal("service %s requires a process limit of 0",
						service_name);
			break;
		case ACL_MASTER_SERVER_PRIVILEGED:
			if (user_name)
				acl_msg_fatal("service %s requires privileged operation",
						service_name);
			break;
		default:
			acl_msg_panic("%s: unknown argument type: %d", myname, key);
		}
	}
	va_end(ap);

	if (root_dir)
		root_dir = acl_var_listener_queue_dir;
	if (user_name)
		user_name = acl_var_listener_owner;

	/*
	 * If not connected to stdin, stdin must not be a terminal.
	 */
	if (stream == 0 && isatty(STDIN_FILENO))
		acl_msg_fatal("%s(%d)->%s: do not run this command by hand",
				__FILE__, __LINE__, myname);

	/*
	 * Can options be required?
	 */
	if (stream == 0) {
		if (transport == 0)
			acl_msg_fatal("no transport type specified");
		if (strcasecmp(transport, ACL_MASTER_XPORT_NAME_INET) == 0) {
			listener_server_accept = listener_server_accept_inet;
			fdtype = ACL_VSTREAM_TYPE_LISTEN | ACL_VSTREAM_TYPE_LISTEN_INET;
		} else if (strcasecmp(transport, ACL_MASTER_XPORT_NAME_UNIX) == 0) {
			listener_server_accept = listener_server_accept_local;
			fdtype = ACL_VSTREAM_TYPE_LISTEN | ACL_VSTREAM_TYPE_LISTEN_UNIX;
#ifdef MASTER_XPORT_NAME_PASS
		} else if (strcasecmp(transport, ACL_MASTER_XPORT_NAME_PASS) == 0) {
			listener_server_accept = listener_server_accept_pass;
			fdtype = ACL_VSTREAM_TYPE_LISTEN;
#endif
		} else {
			acl_msg_fatal("unsupported transport type: %s", transport);
		}
	}

	/*
	 * Retrieve process generation from environment.
	 */
	if ((generation = getenv(ACL_MASTER_GEN_NAME)) != 0) {
		if (!acl_alldig(generation))
			acl_msg_fatal("bad generation: %s", generation);
		sscanf(generation, "%o", &listener_server_generation);
		if (acl_msg_verbose)
			acl_msg_info("process generation: %s (%o)",
					generation, listener_server_generation);
	}

	/*
	 * Traditionally, BSD select() can't handle listenerple processes selecting
	 * on the same socket, and wakes up every process in select(). See TCP/IP
	 * Illustrated volume 2 page 532. We avoid select() collisions with an
	 * external lock file.
	 */

	/*
	 * Set up call-back info.
	 */
	listener_server_service = service;
	listener_server_name = service_name;
	listener_server_argv = argv + optind;

	/*
	 * Run pre-jail initialization.
	 */
	if (chdir(acl_var_listener_queue_dir) < 0)
		acl_msg_fatal("chdir(\"%s\"): %s", acl_var_listener_queue_dir, strerror(errno));
	if (pre_init)
		pre_init(listener_server_name, listener_server_argv);

	acl_chroot_uid(root_dir, user_name);

	__listener_init();

	/*
	 * Run post-jail initialization.
	 */
	if (post_init)
		post_init(listener_server_name, listener_server_argv);

	__eventp = acl_event_new_select(acl_var_listener_delay_sec,
				acl_var_listener_delay_usec);

	/*
	 * Are we running as a one-shot server with the client connection on
	 * standard input? If so, make sure the output is written to stdout so as
	 * to satisfy common expectation.
	 */
	if (stream != 0) {
		service(stream, listener_server_name, listener_server_argv);
		listener_server_exit();
	}

	/*
	 * Running as a semi-resident server. Service connection requests.
	 * Terminate when we have serviced a sufficient number of clients, when
	 * no-one has been talking to us for a configurable amount of time, or
	 * when the master process terminated abnormally.
	 */

	if (acl_var_listener_idle_limit > 0)
		__set_idle_timer(acl_var_listener_idle_limit);

	/* socket count is as same listener_fd_count in parent process */

	__stream_array = (ACL_VSTREAM **)
		acl_mycalloc(ACL_MASTER_LISTEN_FD + socket_count, sizeof(ACL_VSTREAM *));

	for (fd = ACL_MASTER_LISTEN_FD; fd < ACL_MASTER_LISTEN_FD + socket_count; fd++) {
		stream = acl_vstream_fdopen(fd,
					O_RDWR,
					acl_var_listener_buf_size,
					acl_var_listener_rw_timeout,
					fdtype);
		if (stream == NULL)
			acl_msg_fatal("%s(%d)->%s: stream null, fd = %d",
					__FILE__, __LINE__, myname, fd);

		acl_event_enable_read(__eventp,
				stream,
				0,
				listener_server_accept,
				stream);
		acl_close_on_exec(ACL_VSTREAM_SOCK(stream), ACL_CLOSE_ON_EXEC);
	}

	acl_event_enable_read(__eventp, ACL_MASTER_STAT_STREAM, 0, listener_server_abort, (void *) 0);
	acl_close_on_exec(ACL_MASTER_STATUS_FD, ACL_CLOSE_ON_EXEC);
	acl_close_on_exec(ACL_MASTER_FLOW_READ, ACL_CLOSE_ON_EXEC);
	acl_close_on_exec(ACL_MASTER_FLOW_WRITE, ACL_CLOSE_ON_EXEC);
	watchdog = acl_watchdog_create(acl_var_listener_daemon_timeout, (ACL_WATCHDOG_FN) 0, (char *) 0);

	/*
	 * The event loop, at last.
	 */
	while (acl_var_listener_use_limit == 0 || use_count < acl_var_listener_use_limit || client_count > 0) {
	   int  delay_sec;

		if (listener_server_lock != 0) {

			acl_watchdog_stop(watchdog);
			if (acl_myflock(ACL_VSTREAM_FILE(listener_server_lock),
					ACL_INTERNAL_LOCK,
					ACL_MYFLOCK_OP_EXCLUSIVE) < 0)
				acl_msg_fatal("select lock: %s", strerror(errno));
		}
		acl_watchdog_start(watchdog);
		delay_sec = loop ? loop(listener_server_name, listener_server_argv) : -1;
		acl_event_set_delay_sec(__eventp, delay_sec);
		acl_event_loop(__eventp);
	}
	listener_server_exit();
}
#endif /* ACL_UNIX */
