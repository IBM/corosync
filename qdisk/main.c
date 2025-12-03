#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>

#include "algorithm.h"
#include "cmap.h"
#include "log.h"
#include "quorum.h"
#include "vquorum.h"

#include <corosync/quorum.h>
#include <corosync/votequorum.h>
#include <qb/qbloop.h>

#include <systemd/sd-daemon.h>

#define USEC_PER_SEC  UINT64_C(1000000)
#define NANOSECOND_PER_SECOND UINT64_C(1000000000)
#define TIMER_INTERVAL NANOSECOND_PER_SECOND * 1

static uint64_t heartbeat;

static void timer_cb(void *data)
{
	qb_loop_t *ml = (qb_loop_t *)(data);

	// printf("heartbeat %ld\n", time(NULL));

	// schedule the timer for the next run
	if(qb_loop_timer_add(ml, QB_LOOP_HIGH, heartbeat, ml, timer_cb, NULL) < 0) {
		fprintf(stderr, "Failed to add timer to the main loop.\n");
		abort();
	}

	algorithm_run();

	// sd_notifyf(0, "WATCHDOG=1\nSTATUS=%s", algorithm_get_state_name());
	sd_notifyf(0, "STATUS=%s", algorithm_get_state_name());
}



int main(void)
{
	qb_loop_t *ml = NULL;

	INIT_LOG();
	sd_notify(0, "STATUS=Starting votequorum connection\n");
	while(CS_OK != vquorum_init()) {
		ENGN_LOG(LOG_ERR, "An error occured initializing the interface to the votequorum engine");
		sleep(5);
	}
	sd_notify(0, "STATUS=Starting quorum connection\n");
	while(CS_OK != quorum_init()) {
		ENGN_LOG(LOG_ERR, "An error occured initializing the interface to the quorum engine");
		sleep(5);
	}
	sd_notify(0, "STATUS=Starting cmap\n");
	while(CS_OK != qdisk_cmap_init()) {
		ENGN_LOG(LOG_ERR, "An error occured initializing the interface to the cmap engine");
		sleep(5);
	}

	sd_notify(0, "STATUS=Initializing algorithm\n");
	while(PR_ERR_OK != algorithm_init()) {
		ENGN_LOG(LOG_ERR, "An error occured while trying to initialize tiebreaker device");
		sleep(5);
	}
	ENGN_LOG(LOG_NOTICE, "Successfully initialized tiebreaker device");

	ml = qb_loop_create();
	if(ml == NULL) {
		fprintf(stderr, "Failed to create the mainloop\n");
	}

	heartbeat = (uint64_t)algorithm_get_heartbeat() * NANOSECOND_PER_SECOND;

	/* Add a timer to the mainloop to run every 5 seconds*/
	if(qb_loop_timer_add(ml, QB_LOOP_HIGH, heartbeat, ml, timer_cb, NULL) < 0) {
		fprintf(stderr, "Failed to add timer to the main loop.\n");
		abort();
	}

	sd_notifyf(0, "READY=1\nSTATUS=Running");
	// sd_notifyf(0, "READY=1\nSTATUS=Running\nWATCHDOG_USEC=%" PRIu64, (uint64_t)algorithm_get_heartbeat() * 3u * USEC_PER_SEC);

	qb_loop_run(ml);

	sd_notify(0, "STOPPING=1");

	qb_loop_destroy(ml);

	algorithm_stop();

	qb_log_fini();
	return 0;
}
