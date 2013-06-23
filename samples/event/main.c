#include "lib_acl.h"

static void trigger_event(int event acl_unused, void *context acl_unused)
{
	printf("timer trigger now\r\n");
}

int main(int argc acl_unused, char *argv[] acl_unused)
{
	ACL_EVENT *eventp;
	int   i;

	acl_msg_stdout_enable(1);
	eventp = acl_event_new_select(1, 0);

	for (i = 0; i < 100; i++) {
		acl_event_request_timer(eventp, trigger_event, NULL, 100, 1);
		/*
		acl_event_cancel_timer(eventp, trigger_event, NULL);
		*/
	}

	while (1) {
		acl_event_loop(eventp);
	}

	return (0);
}

