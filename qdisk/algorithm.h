#ifndef ENGN_ALGORITHM_H_
#define ENGN_ALGORITHM_H_

#include <stdint.h>
#include <unistd.h>

#include <corosync/quorum.h>

#include "persistent_reserve/device.h"

#ifdef __cplusplus
extern "C" {
#endif

/// the maximum number of retries to create detect and create the device handle
#define ALGORITHM_DEVICE_CREATE_MAX_RETRY 10

void quorum_callback(quorum_handle_t handle, uint32_t quorate,
                     uint64_t ring_seq, uint32_t view_list_entries,
                     uint32_t *view_list);

void algorithm_run(void);
pr_dev_err algorithm_init(void);
uint32_t algorithm_get_heartbeat(void);
const char *algorithm_get_state_name(void);
void algorithm_stop(void);

#ifdef __cplusplus
}
#endif

#endif
