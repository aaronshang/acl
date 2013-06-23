#include "lib_acl.h"
#include "lib_protocol.h"

#include "global.h"
#include "gid_oper.h"
#include "cmdline_service.h"
#include "http_service.h"
#include "service_main.h"

/* 配置文件项 */

/* TODO: you can add configure items here */

int   var_cfg_debug_mem;
int   var_cfg_loop_enable;
int   var_cfg_sync_gid;
int   var_cfg_use_http;

ACL_CONFIG_BOOL_TABLE service_conf_bool_tab[] = {
	/* TODO: you can add configure variables of (bool) type here */
	{ "debug_mem", 0, &var_cfg_debug_mem },
	{ "loop_enable", 0, &var_cfg_loop_enable },
	{ "sync_gid", 1, &var_cfg_sync_gid },
	{ "use_http", 1, &var_cfg_use_http },
	{ 0, 0, 0 },
};

int   var_cfg_debug_section;
int   var_cfg_gid_step;
int   var_cfg_gid_test;
int   var_cfg_fh_limit;
int   var_cfg_io_timeout;

ACL_CONFIG_INT_TABLE service_conf_int_tab[] = {
	/* TODO: you can add configure variables of int type here */
	{ "debug_section", 120, &var_cfg_fh_limit, 0, 0 },
	{ "gid_step", 1, &var_cfg_gid_step, 0, 0 },
	{ "gid_test", 50000, &var_cfg_gid_test, 0, 0 },
	{ "fh_limit", 100, &var_cfg_fh_limit, 0, 0 },
	{ "io_timeout", 30, &var_cfg_io_timeout, 0, 0 },
	{ 0, 0, 0, 0, 0 },
};

char *var_cfg_gid_path;

ACL_CONFIG_STR_TABLE service_conf_str_tab[] = {
	/* TODO: you can add configure variables of (char *) type here */
	{ "gid_path", "./var", &var_cfg_gid_path },
	{ 0, 0, 0 },
};

/* 初始化函数 */
void service_init(void *ctx acl_unused)
{
	if (var_cfg_debug_mem) {
		acl_memory_debug_start();
		acl_memory_debug_stack(1);
	}

	gid_init(var_cfg_fh_limit, var_cfg_sync_gid, var_cfg_debug_section);
}

void service_exit(void *ctx acl_unused)
{
	gid_finish();
}

int service_main(ACL_VSTREAM *client, void *ctx acl_unused)
{
	int   (*service)(ACL_VSTREAM*);
	int   ret;

	if (var_cfg_use_http)
		service = http_service;
	else
		service = cmdline_service;

	if (var_cfg_loop_enable) {
		while (1) {
			if ((ret = service(client)) != 1)
			{
				return (-1);  /* 返回-1要求框架关闭该连接 */
			}
		}
	} else if (service(client) != 1)
		return (-1);
	else
		return (0);
}
