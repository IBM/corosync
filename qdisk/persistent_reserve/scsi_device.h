#ifndef SCSI_PERSISTENT_RESERVE_DEVICE_H
#define SCSI_PERSISTENT_RESERVE_DEVICE_H

#include "device.h"

#ifdef __cplusplus
extern "C" {
#endif

//reservation types
enum scsi_pr_device_reservation_type { 
    SCSI_WRITE_EXCLUSIVE=1,
    SCSI_EXCLUSIVE_ACCESS=3,
    SCSI_WRITE_EXCLUSIVE_REGISTRANTS_ONLY=5,
    SCSI_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY=6,
    SCSI_WRITE_EXCLUSIVE_ALL_REGISTRANTS=7,
    SCSI_EXCLUSIVE_ACCESS_ALL_REGISTRANTS=8
};

struct persistent_reserve_device* scsi_pr_device_init(const char* device_name, const char *key_path);
void scsi_pr_device_delete(struct persistent_reserve_device* device);

#ifdef __cplusplus
}
#endif


#endif
