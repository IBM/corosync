#ifndef ENGN_QDISK_H_
#define ENGN_QDISK_H_

#include <unistd.h>
#include <stdint.h>
#include "persistent_reserve/device.h"

#include <corosync/cmap.h>
#include <corosync/quorum.h>
#include <corosync/votequorum.h>

#ifdef __cplusplus
extern "C" {
#endif

enum qdevice_disk_instance_state {
    QDEVICE_DISK_SATE_IDLE,
    QDEVICE_DISK_SATE_RESERVE_DISK,
    QDEVICE_DISK_SATE_RECV_VOTE,
    QDEVICE_DISK_SATE_HAVE_QDISK,
    QDEVICE_DISK_SATE_RELEASE_QDISK,
};


struct qdisk_instance
{
	uint32_t nodeid;
	struct persistent_reserve_device *pr_device;

	// cmap
	cmap_handle_t cmap_handle;

	// Config
	char *dev_wwn;
	char *device_path;
	char *reg_key_file;

	// Algorithm
	enum qdevice_disk_instance_state state;
	uint32_t node_count;
	uint32_t algorithm_heartbeat;
	uint32_t algorithm_timeout;

	// Votequorum
	votequorum_handle_t vq_handle;
	struct votequorum_info vq_info;
	votequorum_ring_id_t vq_ring_id;

	uint32_t vq_expected_votes;
	uint32_t vq_node_list_capacity;
	uint32_t vq_node_list_entries;
	uint32_t *vq_node_list;

	// Quorum
	quorum_handle_t quorum_handle;
};

int qdisk_instance_init(struct qdisk_instance *instance,
                        const char *device_path, const char *keyfile,
                        uint32_t heartbeat, uint32_t timeout);
int qdisk_instance_init_from_cmap(struct qdisk_instance *instance);

#ifdef __cplusplus
}
#endif

#endif
