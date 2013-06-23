
#ifndef	__ACL_LISTENER_PARAMS_INCLUDE_H_
#define	__ACL_LISTENER_PARAMS_INCLUDE_H_

#ifdef	__cplusplus
extern "C" {
#endif

#include "stdlib/acl_define.h"
#ifdef ACL_UNIX

extern int   acl_var_listener_pid;
extern char *acl_var_listener_procname;
extern char *acl_var_listener_log_file;

#define	ACL_VAR_LISTENER_BUF_SIZE		"listener_buf_size"
#define	ACL_DEF_LISTENER_BUF_SIZE		81920
extern int   acl_var_listener_buf_size;

#define	ACL_VAR_LISTENER_RW_TIMEOUT		"listener_rw_timeout"
#define	ACL_DEF_LISTENER_RW_TIMEOUT		30
extern int   acl_var_listener_rw_timeout;

#define	ACL_VAR_LISTENER_IN_FLOW_DELAY		"listener_in_flow_delay"
#define	ACL_DEF_LISTENER_IN_FLOW_DELAY		1
extern int   acl_var_listener_in_flow_delay;

#define	ACL_VAR_LISTENER_IDLE_LIMIT		"listener_idle_limit"
#define	ACL_DEF_LISTENER_IDLE_LIMIT		180
extern int   acl_var_listener_idle_limit;

#define	ACL_VAR_LISTENER_QUEUE_DIR		"listener_queue_dir"
#define	ACL_DEF_LISTENER_QUEUE_DIR		"/opt/acl_master/var/queue"
extern char *acl_var_listener_queue_dir;

#define	ACL_VAR_LISTENER_PID_DIR		"listener_pid_dir"
#define	ACL_DEF_LISTENER_PID_DIR		"/opt/acl_master/var/pid"
extern char *acl_var_listener_pid_dir;

#define	ACL_VAR_LISTENER_OWNER			"listener_owner"
#define	ACL_DEF_LISTENER_OWNER			"listener"
extern char *acl_var_listener_owner;

#define	ACL_VAR_LISTENER_DELAY_SEC		"listener_delay_sec"
#define	ACL_DEF_LISTENER_DELAY_SEC		1
extern int   acl_var_listener_delay_sec;

#define	ACL_VAR_LISTENER_DELAY_USEC		"listener_delay_usec"
#define	ACL_DEF_LISTENER_DELAY_USEC		5000
extern int   acl_var_listener_delay_usec;

#define	ACL_VAR_LISTENER_DAEMON_TIMEOUT		"listener_daemon_timeout"
#define	ACL_DEF_LISTENER_DAEMON_TIMEOUT		1800
extern int   acl_var_listener_daemon_timeout;

#define	ACL_VAR_LISTENER_USE_LIMIT		"listener_use_limit"
#define	ACL_DEF_LISTENER_USE_LIMIT		10
extern int   acl_var_listener_use_limit;

#endif /* ACL_UNIX */

#ifdef	__cplusplus
}
#endif

#endif

