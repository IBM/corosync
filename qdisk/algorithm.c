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

#include "algorithm.h"
#include "persistent_reserve/persistent_reserve.h"
#include "cmap.h"
#include "log.h"
#include "quorum.h"
#include "vquorum.h"

#include <limits.h>
#include <stdbool.h>

static uint64_t gettime_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec*UINT64_C(1000) + t.tv_nsec / UINT64_C(1000000);
}

static uint32_t is_quorate = 0;

static enum state { START, IDLE, RESERVE_DISK, RECV_VOTE, HAVE_QDISK, RELEASE_QDISK, QDISK_NUM_STATES_ } state = START;

static const char *get_algorithm_state_name(int s)
{
	static const char *state_strings[] = { "START", "IDLE", "RESERVE_DISK", "RECV_VOTE", "HAVE_QDISK", "RELEASE_QDISK" };
	if(s < 0 || s >= QDISK_NUM_STATES_) {
		ENGN_LOG(LOG_CRIT, "Unknown state number %i!\n", state);
		abort();
	}
	return state_strings[s];
}

static struct persistent_reserve_device *pr_device = NULL;
static uint32_t node_count, algorithm_heartbeat, algorithm_timeout;

void quorum_callback(quorum_handle_t handle, uint32_t quorate, uint64_t ring_seq, uint32_t view_list_entries, uint32_t *view_list)
{
	(void)handle;
	(void)quorate;
	(void)ring_seq;
	(void)view_list_entries;
	(void)view_list;
	is_quorate = quorate;
	vquorum_qdisk_share_ikey(persistent_reserve_device_get_key(pr_device));
}

/// Check that all nodes we can see are registered to the same disk as us
static pr_dev_err validate_node_keys()
{
	pr_dev_err err = PR_ERR_OK;
	size_t num_registered_keys = 0;
	uint64_t *registered_keys = NULL;
	err = persistent_reserve_device_get_registered_keys(pr_device, &num_registered_keys, &registered_keys);
	if(PR_ERR_OK != err) {
		ENGN_LOG(LOG_ERR, "Failed to fetch current registered keys.");
		return err;
	}

	uint32_t num_nodes = 0;
	uint32_t *nodelist = NULL;
	vquorum_get_node_list(&num_nodes, &nodelist); // doesn't need freeing, no allocation
	for(uint32_t i=0; i < num_nodes; i++) { // loop over nodes
		uint64_t node_key = 0;
		if(CS_OK == vquorum_get_node_key(nodelist[i], &node_key) && node_key) {
			bool found = false;
			for(size_t j=0; j < num_registered_keys && !found; j++) {
				found = (node_key == registered_keys[j]);
			}
			if(!found) {
				ENGN_LOG(LOG_ERR, "Node %i has reservation key %" PERSISTENT_RESERVE_PRI_KEY " which was not registered on the disk\n", 
				         nodelist[i], node_key);
				err = PR_ERR_FOUND_UNREGISTERED_NODE_KEY;
				goto out;
			}
		}
	}

out:
	if(registered_keys) {
		free(registered_keys);
		registered_keys = NULL;
	}
	return err;
}

pr_dev_err algorithm_init(void)
{
	char *device = NULL;
	char *key_file = NULL;
	char *reserve_key_str = NULL;
	uint8_t qdisk_can_operate = 0;
	pr_dev_err err = PR_ERR_OK;
	cs_error_t cs_err = CS_OK;
	uint64_t ikey = 0;

	qdisk_cmap_get_qdisk_can_operate(&qdisk_can_operate);
	if(!qdisk_can_operate) {
		ENGN_LOG(LOG_ERR, "QDisk is not able to operate. Verify the configuration in corosync.conf");
		return PR_ERR_DEVICE_NOT_OPERABLE;
	}

	if(CS_OK != qdisk_cmap_get_qdisk_device(&device)) {
		ENGN_LOG(LOG_ERR, "Failed to get device path.  Verify the configuration in corosync.conf");
		return PR_ERR_DEVICE_NOT_OPERABLE;
	}

	qdisk_cmap_get_heartbeat(&algorithm_heartbeat);
	qdisk_cmap_get_timeout(&algorithm_timeout);
	ENGN_LOG(LOG_NOTICE, "QDisk device set to: '%s'\n", device);
	ENGN_LOG(LOG_NOTICE, "Algorithm heartbeat set to: %ums\n", algorithm_heartbeat);
	ENGN_LOG(LOG_NOTICE, "Algorithm timeout set to: %ums\n", algorithm_timeout);

	if(!is_scsi_device(device) && !is_nvme_device(device)) {
		ENGN_LOG(LOG_ERR, "The disk is neither SCSI nor NVME. Device detection failed");
		free(device);
		return PR_ERR_DEVICE_INVALID_DEVICE_TYPE;
	}

	if(CS_OK == qdisk_cmap_get_qdisk_keystr(&reserve_key_str)) {
		int scanf_res = sscanf(reserve_key_str, "%" PERSISTENT_RESERVE_PRI_KEY, &ikey);
		free(reserve_key_str);
		reserve_key_str = NULL;
		if(scanf_res != 1) {
			ENGN_LOG(LOG_ERR, "QDisk key string has bad format. Verify the configuration in corosync.conf");
			return PR_ERR_DEVICE_NOT_OPERABLE;
		}
	} else if(CS_OK != qdisk_cmap_get_qdisk_key_file(&key_file)) {
		key_file = strdup(PERSISTENT_RESERVE_DEFAULT_KEYFILE);
	}

	if(key_file) {
		ENGN_LOG(LOG_NOTICE, "Registration Keyfile set to: %s\n", key_file);
		pr_device = persistent_reserve_device_new(device, key_file);
		free(key_file);
		key_file = NULL;
	} else {
		pr_device = persistent_reserve_device_new_key(device, ikey);
	}
	free(device);
	if(NULL == pr_device) {
		ENGN_LOG(LOG_ERR, "Could not allocate memory for device handle");
		err = PR_ERR_DEVICE_MEMORY_ALLOCATION_FAILED;
	}

	ENGN_LOG(LOG_NOTICE, "Registration Key set to: %" PERSISTENT_RESERVE_PRI_KEY "\n",
	         persistent_reserve_device_get_key(pr_device));

	err = validate_node_keys();
	if(PR_ERR_OK != err) {
		return err; // validate_node_keys() will log a message
	}

	err = persistent_reserve_device_register_key(pr_device);
	if( PR_ERR_OK != err) {
		ENGN_LOG(LOG_ERR, "Failed to register our key.");
		return err;
	}
	ENGN_LOG(LOG_NOTICE, "Registered with disk...\n");

	cs_err = vquorum_qdisk_share_ikey(persistent_reserve_device_get_key(pr_device));
	if(CS_OK != cs_err) {
		persistent_reserve_device_unregister_key(pr_device);
		ENGN_LOG(LOG_ERR, "Failed to share our key.");
		return err;
	}

	ENGN_LOG(LOG_NOTICE, "Shared key with cluster...\n");

	qdisk_cmap_get_node_count(&node_count);

	return PR_ERR_OK;
}

uint32_t algorithm_get_heartbeat(void)
{
	return algorithm_heartbeat;
}

const char *algorithm_get_state_name(void)
{
	return get_algorithm_state_name(state);
}

void algorithm_run(void)
{
	int reserved = 0;
	bool recv_flag_set = false;
	unsigned int votes = 0;
	int prev_state = state;
	static uint64_t timeout_time = 0;
	pr_dev_err err = PR_ERR_OK;

	quorum_dispatch_all();

	votes = vquorum_get_total_votes();
	reserved = persistent_reserve_device_is_reserved(pr_device);
	recv_flag_set = vquorum_is_qdisk_recv_vote_flag_set();

	if(reserved < 0) {
		reserved = 0;
		ENGN_LOG(LOG_ERR, "Failed to check device reservation status\n");
	}

	ENGN_LOG(LOG_DEBUG, "State: %s Votes: %d Nodes: %d Reserved:%d RFlag:%d\n",
	         get_algorithm_state_name(state), votes, node_count, reserved, recv_flag_set);

	switch(state) {
	case START:
		if(PR_ERR_FOUND_UNREGISTERED_NODE_KEY == validate_node_keys()) { // Check for disk misconfiguration
			abort(); // validate_node_keys() will log a message
		}
		if(is_quorate) {
			if(reserved) { // Safe to abort the reservation since it is no longer needed when we are quorate
				persistent_reserve_device_abort(pr_device);
			}
			state = IDLE;
		}
		break;
	case IDLE:
		if(votes*2 == node_count && !reserved) {
			timeout_time = gettime_ms() + algorithm_timeout;
			state = RESERVE_DISK;
		} else if(recv_flag_set) {
			timeout_time = gettime_ms() + algorithm_timeout;
			state = RECV_VOTE;
		}
		if(PR_ERR_FOUND_UNREGISTERED_NODE_KEY == validate_node_keys()) {
			abort(); // validate_node_keys() will log a message
		}
		break;
	case RESERVE_DISK:
		err = persistent_reserve_device_reserve(pr_device);

		if(PR_ERR_OK == err) {
			ENGN_LOG(LOG_NOTICE, "Successfullly made disk reservation on %s\n",
			         persistent_reserve_device_get_name(pr_device));
			vquorum_qdisk_cast_vote(); // ignore error, we will cast again in the next state
			state = HAVE_QDISK;
		} else if(gettime_ms() > timeout_time) {
			ENGN_LOG(LOG_NOTICE, "Returning to idle after %d seconds\n", algorithm_timeout);
			state = IDLE;
		} else if(recv_flag_set) {
			state = RECV_VOTE;
		} else {
			ENGN_LOG(LOG_NOTICE, "Failed disk reservation on %s, will retry\n",
			         persistent_reserve_device_get_name(pr_device));
		}
		break;
	case RECV_VOTE:
		if(recv_flag_set) {
			timeout_time = gettime_ms() + algorithm_timeout;
		} else if(gettime_ms() > timeout_time) {
			ENGN_LOG(LOG_NOTICE, "Returning to idle after %d seconds\n", algorithm_timeout);
			state = IDLE;
		}
		break;
	case HAVE_QDISK:
		err = persistent_reserve_device_reserve(pr_device);
		if(PR_ERR_OK == err && votes == node_count / 2 + 1) {
			vquorum_qdisk_cast_vote(); // ignore error, will try again next timeout.
		} else if(votes != node_count / 2 + 1) {
			state = RELEASE_QDISK;
		}
		break;
	case RELEASE_QDISK:
		if(PR_ERR_OK == persistent_reserve_device_release(pr_device)) {
			state = IDLE;
		}
		break;
	}

	// log state changes
	if(prev_state != state) {
		ENGN_LOG(LOG_NOTICE, "Votes: %d Nodes: %d Reserved:%d RFlag:%d\n",
		         votes, node_count, reserved, recv_flag_set);
		ENGN_LOG(LOG_NOTICE, "State Change: %s->%s",
		         get_algorithm_state_name(prev_state),
		         get_algorithm_state_name(state));
	}
}

void algorithm_stop(void)
{
	if(pr_device) {
		persistent_reserve_device_release(pr_device);
		persistent_reserve_device_unregister_key(pr_device);
		persistent_reserve_device_free(pr_device);
	}
}
