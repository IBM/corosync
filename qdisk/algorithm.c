
#include "algorithm.h"
#include "persistent_reserve/nvme_device.h"
#include "persistent_reserve/scsi_device.h"
#include "cmap.h"
#include "log.h"
#include "quorum.h"
#include "vquorum.h"

#include <limits.h>
#include <stdbool.h>

static enum state { IDLE, RESERVE_DISK, RECV_VOTE, HAVE_QDISK, RELEASE_QDISK } state;

static const char *get_algorithm_state_name(int s)
{
	static const char *state_strings[] = { "IDLE", "RESERVE_DISK", "RECV_VOTE", "HAVE_QDISK", "RELEASE_QDISK" };
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
	//struct qdisk_context *ctx = NULL;
	//quorum_context_get(handel, &ctx);
	// vquorum_qdisk_share_ikey(persistent_reserve_device_get_ikey(ctx->pr_device));
	vquorum_qdisk_share_ikey(persistent_reserve_device_get_ikey(pr_device));
}

static const char *default_key_file = "/var/lib/misc/reserve_key";

pr_dev_err algorithm_init(void)
{
	char *dev_wwn = NULL;
	char *device = NULL;
	char *key_file = NULL;
	uint8_t qdisk_can_operate = 0;
	pr_dev_err err = PR_ERR_OK;
	cs_error_t cs_err = CS_OK;

	INIT_LOG();

	qdisk_cmap_get_qdisk_can_operate(&qdisk_can_operate);
	if(!qdisk_can_operate) {
		ENGN_LOG(LOG_ERR, "QDisk is not able to operate.  Verify the configuration in corosync.conf");
		return PR_ERR_DEVICE_NOT_OPERABLE;
	}

	if(CS_OK == qdisk_cmap_get_qdisk_device_wwn(&dev_wwn)) {
		device = calloc(PATH_MAX+1, sizeof(char));
		snprintf(device, PATH_MAX, "/dev/disk/by-id/wwn-0x%s", dev_wwn);
	}
	else if(CS_OK != qdisk_cmap_get_qdisk_device(&device)) {
		ENGN_LOG(LOG_ERR, "Failed to get device path.  Verify the configuration in corosync.conf");
		return PR_ERR_DEVICE_NOT_OPERABLE;
	}

	if(CS_OK != qdisk_cmap_get_qdisk_key_file(&key_file)) {
		key_file = strdup(default_key_file);
	}
	qdisk_cmap_get_heartbeat(&algorithm_heartbeat);
	qdisk_cmap_get_timeout(&algorithm_timeout);
	ENGN_LOG(LOG_NOTICE, "QDisk device set to:%s\n", device);
	ENGN_LOG(LOG_NOTICE, "Algorithm heartbeat set to:%u\n", algorithm_heartbeat);
	ENGN_LOG(LOG_NOTICE, "Algorithm timeout set to:%u\n", algorithm_timeout);

	for(int i = 0; i < ALGORITHM_DEVICE_CREATE_MAX_RETRY && NULL == pr_device; i++) {
		err = PR_ERR_OK;
		if(is_scsi_device(device)) {
			pr_device = scsi_pr_device_init(device, key_file);
			if(NULL == pr_device) {
				ENGN_LOG(LOG_ERR, "Could not allocate memory for device handle");
				err = PR_ERR_DEVICE_MEMORY_ALLOCATION_FAILED;
			}
			ENGN_LOG(LOG_INFO, "Using SCSI...\n");

		} else if(is_nvme_device(device)) {
			pr_device = nvme_pr_device_init(device, key_file);
			if(NULL == pr_device) {
				ENGN_LOG(LOG_ERR, "Could not allocate memory for device handle");
				err = PR_ERR_DEVICE_MEMORY_ALLOCATION_FAILED;
			}
			ENGN_LOG(LOG_INFO, "Using NVME...\n");
		} else {
			ENGN_LOG(LOG_ERR, "The disk is neither SCSI nor NVME. Device detection failed");
			err = PR_ERR_DEVICE_INVALID_DEVICE_TYPE;
		}
		sleep(1);
	}
	free(device);
	if(key_file) {
		free(key_file);
	}

	if(PR_ERR_OK != err) {
		return err;
	}

	// Check that all nodes we can see are registered to the same disk as us
	size_t num_registered_keys = 0;
	uint64_t *registered_keys = NULL;
	err = persistent_reserve_device_get_registered_keys(pr_device, &num_registered_keys, &registered_keys);
	if(PR_ERR_OK != err) {
		// TODO: cleanup
		ENGN_LOG(LOG_ERR, "Failed to fetch current registered Keys.");
		return err;
	}

	uint32_t num_nodes = 0;
	uint32_t *nodelist = NULL;
	vquorum_get_node_list(&num_nodes, &nodelist);
	for(uint32_t i=0; i < num_nodes; i++) { // loop over nodes
		uint64_t node_key = 0;
		if(CS_OK == vquorum_get_node_key(nodelist[i], &node_key) && node_key) {
			bool found = false;
			for(size_t j=0; j < num_registered_keys && !found; j++) {
				found = (node_key == registered_keys[j]);
			}
			if(!found) {
				ENGN_LOG(LOG_ERR, "Node %i has reservation key %" PERSISTENT_RESERVE_PRI_KEY " which was not registered on the disk\n", i, node_key);
				return PR_ERR_FOUND_UNREGISTERED_NODE_KEY;
			}
		}
	}

	err = persistent_reserve_device_register_key(pr_device);
	if ( PR_ERR_OK != err) {
		ENGN_LOG(LOG_ERR, "Failed to register our key.");
		return err;
	}
	cs_err = vquorum_qdisk_share_ikey(persistent_reserve_device_get_ikey(pr_device));
	if(CS_OK != cs_err) {
		ENGN_LOG(LOG_ERR, "Failed to share our key.");
		return PR_ERR_REGISTRATION_FAILED;
	}

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
	static time_t timeout_time;
	pr_dev_err err = PR_ERR_OK;

	quorum_dispatch_all();

	votes = vquorum_get_total_votes();
	reserved = persistent_reserve_device_is_reserved(pr_device);
	recv_flag_set = vquorum_is_qdisk_recv_vote_flag_set();

	if(reserved < 0) {
		reserved = 0;
		ENGN_LOG(LOG_ERR, "Failed to check device reservation status\n");
	}

	ENGN_LOG(LOG_NOTICE, "State: %s Votes: %d Nodes: %d Reserved:%d RFlag:%d\n", get_algorithm_state_name(state), votes, node_count, reserved, recv_flag_set);

	switch(state) {
	case IDLE:
		if(votes == node_count / 2 && !reserved) {
			timeout_time = time(NULL) + algorithm_timeout;
			state = RESERVE_DISK;
		} else if(recv_flag_set) {
			timeout_time = time(NULL) + algorithm_timeout;
			state = RECV_VOTE;
		}
		break;
	case RESERVE_DISK:
		err = persistent_reserve_device_reserve(pr_device);

		if(PR_ERR_OK == err) {
			ENGN_LOG(LOG_NOTICE, "Successfullly made disk reservation on %s\n", persistent_reserve_device_get_name(pr_device));
			vquorum_qdisk_cast_vote(); // ignore error, we will cast again in the next state
			state = HAVE_QDISK;
		} else if(time(NULL) > timeout_time) {
			printf("Returning to idle after %d seconds\n", algorithm_timeout);
			state = IDLE;
		} else if(recv_flag_set) {
			state = RECV_VOTE;
		} else {
			ENGN_LOG(LOG_NOTICE, "Failed disk reservation on %s, will retry\n", persistent_reserve_device_get_name(pr_device));
		}
		break;
	case RECV_VOTE:
		if(recv_flag_set) {
			timeout_time = time(NULL) + algorithm_timeout;
		} else if(time(NULL) > timeout_time) {
			printf("Returning to idle after %d seconds\n", algorithm_timeout);
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

	// TODO: check if we hold reservation when we shouldn't and realease it
	// if(state != HAVE_QDISK && state != RECV_VOTE && reserved && )

	// log state changes
	if(prev_state != state) {
		ENGN_LOG(LOG_NOTICE, "State Change: %s->%s", get_algorithm_state_name(prev_state), get_algorithm_state_name(state));
	}
}

void algorithm_stop(void)
{
	if(pr_device) {
		persistent_reserve_device_release(pr_device);
		// persistent_reserve_device_unregister_key(pr_device);
	}

	// vquorum_qdisk_clear_vote();
}
