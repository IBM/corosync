/*
 * Copyright (c) 2025 IBM.
 *
 * All rights reserved.
 *
 * Author: Thomas Jones (thomas.jones@ibm.com)
 *         Michael Baker
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the Red Hat, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>
#include <limits.h>
#include <libgen.h>
#include <stdlib.h>

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
#define USEC_PER_MS  UINT64_C(1000)
#define NANOSECOND_PER_SECOND UINT64_C(1000000000)
#define NANOSECOND_PER_MS UINT64_C(1000000)

static char blackbox_path[PATH_MAX] = "/var/log/corosync-qdisk.fdata";

static void timer_cb(void *data)
{
	qb_loop_t *mloop = (qb_loop_t *)(data);

	// schedule the timer for the next run
	int32_t err = qb_loop_timer_add(mloop, QB_LOOP_HIGH,
	                                (uint64_t)algorithm_get_heartbeat() * NANOSECOND_PER_MS,
	                                mloop, timer_cb, NULL);
	if(err < 0) {
		ENGN_LOG(LOG_CRIT, "Failed to add timer to the main loop.\n");
		abort();
	}

	char *logfile_path = NULL;
	if(CS_OK == qdisk_cmap_get_corosync_logfile(&logfile_path)) {
		// Update the black box path with a new timestamp
		char path_buf[PATH_MAX];
		if(realpath(logfile_path, path_buf)) {
			char *dir = dirname(path_buf);
			char timestr[30] = { 0 }; // 29 chars + '\0'
			time_t now = time(NULL);
			strftime(timestr, 30, "%F-%T%z", localtime(&now));
			snprintf(blackbox_path, PATH_MAX, "%s/corosync-qdiskd-%s.fdata", dir, timestr);
		}
		free(logfile_path);
	}

	algorithm_run();

	// sd_notifyf(0, "WATCHDOG=1\nSTATUS=%s", algorithm_get_state_name());
	sd_notifyf(0, "STATUS=%s", algorithm_get_state_name());
}

static int32_t please_exit_fn(int32_t rsignal, void *data)
{(void)rsignal;
	qb_loop_t *ml = (qb_loop_t *) data;
	printf("Shutting down...\n");
	qb_loop_stop(ml);
	return QB_FALSE;
}

static int32_t dump_fn(int32_t rsignal, void *data)
{(void)rsignal; (void)data;
	qb_log_blackbox_write_to_file(blackbox_path);
	return QB_TRUE;
}

static void sigsegv_handler(int sig)
{
	// We're in a bad state, try to write blackbox and finish our death
	(void)signal(SIGSEGV, SIG_DFL);
	(void)signal(SIGABRT, SIG_DFL);
	qb_log_blackbox_write_to_file(blackbox_path);
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log(LOG_CRIT, "FATAL SIGNAL: aborting!\n");
	qb_log_fini();
	raise(sig);
}



int main(void)
{
	qb_loop_t *mloop = NULL;

	sd_notify(0, "STATUS=Starting...\n");

	(void)signal (SIGSEGV, sigsegv_handler);
	(void)signal (SIGABRT, sigsegv_handler);

	qb_log_init("tbdisk", LOG_DAEMON, LOG_INFO);

	qb_log_filter_ctl(TAGS_MAIN, QB_LOG_TAG_SET, QB_LOG_FILTER_FILE, "main.c", LOG_TRACE);
	qb_log_filter_ctl(TAGS_ALGORITHM, QB_LOG_TAG_SET, QB_LOG_FILTER_FILE, "algorithm.c", LOG_TRACE);
	qb_log_filter_ctl(TAGS_VQUORUM, QB_LOG_TAG_SET, QB_LOG_FILTER_FILE, "vquorum.c", LOG_TRACE);

	qb_log_tags_stringify_fn_set(qdisk_log_tag_to_string); // setup the callback to print custom tags
	/*set the logging format of the syslog messages.  Note that SYSLOG logging is enabled by default.*/
	qb_log_format_set(QB_LOG_SYSLOG,"[%g]file:%f,ln:%l  %b");

	/*setup blackbox handling*/
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_TRUE);
	qb_log_filter_ctl(QB_LOG_BLACKBOX, QB_LOG_FILTER_CLEAR_ALL, QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_filter_ctl(QB_LOG_BLACKBOX, QB_LOG_FILTER_ADD, QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_SIZE, 16*1024);
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_THREADED, QB_FALSE);

	if(CS_OK != qdisk_cmap_init()) {
		ENGN_LOG(LOG_ERR, "An error occured initializing the interface to the cmap engine\n");
		return 1;
	}

	char *logfile_path = NULL;
	if(CS_OK == qdisk_cmap_get_corosync_logfile(&logfile_path)) {
		int32_t file_target = qb_log_file_open(logfile_path);
		qb_log_format_set(file_target,"%T [%P] %H %N %p [%5g] %f:%n:%l  %b");
		qb_log_filter_ctl(file_target, QB_LOG_FILTER_ADD, QB_LOG_FILTER_FILE, "*", LOG_INFO);
		qb_log_ctl(file_target, QB_LOG_CONF_ENABLED, QB_TRUE);

		// Update the black box path with a new timestamp
		char path_buf[PATH_MAX];
		if(realpath(logfile_path, path_buf)) {
			char *dir = dirname(path_buf);
			char timestr[30] = { 0 }; // 29 chars + '\0'
			time_t now = time(NULL);
			strftime(timestr, 30, "%F-%T%z", localtime(&now));
			snprintf(blackbox_path, PATH_MAX, "%s/corosync-qdiskd-%s.fdata", dir, timestr);
		}
		free(logfile_path);
	}

	mloop = qb_loop_create();
	if(mloop == NULL) {
		ENGN_LOG(LOG_ERR, "Failed to create the mainloop\n");
		abort();
	}

	qb_loop_signal_add(mloop, QB_LOOP_HIGH, SIGINT, mloop, please_exit_fn, NULL);
	qb_loop_signal_add(mloop, QB_LOOP_HIGH, SIGQUIT, mloop, please_exit_fn, NULL);
	qb_loop_signal_add(mloop, QB_LOOP_HIGH, SIGTERM, mloop, please_exit_fn, NULL);
	qb_loop_signal_add(mloop, QB_LOOP_HIGH, SIGUSR1, mloop, dump_fn, NULL);

	sd_notify(0, "STATUS=Starting votequorum connection\n");
	if(CS_OK != vquorum_init()) {
		ENGN_LOG(LOG_ERR, "An error occured initializing the interface to the votequorum engine\n");
		return 1;
	}
	sd_notify(0, "STATUS=Starting quorum connection\n");
	if(CS_OK != quorum_init()) {
		ENGN_LOG(LOG_ERR, "An error occured initializing the interface to the quorum engine\n");
		return 1;
	}
	sd_notify(0, "STATUS=Initializing algorithm\n");
	if(PR_ERR_OK != algorithm_init()) {
		ENGN_LOG(LOG_ERR, "An error occured while trying to initialize tiebreaker device\n");
		return 1;
	}
	ENGN_LOG(LOG_NOTICE, "Successfully initialized tiebreaker device\n");

	/* Add a timer to the mainloop to run every heartbeat milliseconds*/
	int32_t err = qb_loop_timer_add(mloop, QB_LOOP_HIGH,
	                                (uint64_t)algorithm_get_heartbeat() * NANOSECOND_PER_MS,
	                                mloop, timer_cb, NULL);
	if(err < 0) {
		ENGN_LOG(LOG_ERR, "Failed to add timer to the main loop.\n");
		abort();
	}

	sd_notifyf(0, "READY=1\nSTATUS=Running");
	// sd_notifyf(0, "READY=1\nSTATUS=Running\nWATCHDOG_USEC=%\nWATCHDOG=1\n" PRIu64, (uint64_t)algorithm_get_heartbeat() * 3u * USEC_PER_MS);

	qb_loop_run(mloop);

	sd_notify(0, "STOPPING=1");

	qb_loop_destroy(mloop);

	algorithm_stop();

	ENGN_LOG(LOG_NOTICE, "Qdisk Stopped\n");

	qb_log_fini();

	return 0;
}
