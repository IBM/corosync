#ifndef __NVME_PERSISTENT_RESERVE_DEVICE_H
#define __NVME_PERSISTENT_RESERVE_DEVICE_H

#include "device.h"

#ifdef __cplusplus
extern "C" {
#endif

//#define DEBUG
#define NVMEPRDEV_KEY_LENGTH 16

//reservation types
enum NVMEPRDev_ReservationType { 
    NVME_WRITE_EXCLUSIVE=1,
    NVME_EXCLUSIVE_ACCESS=2,
    NVME_WRITE_EXCLUSIVE_REGISTRANTS_ONLY=3,
    NVME_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY=4,
    NVME_WRITE_EXCLUSIVE_ALL_REGISTRANTS=5,
    NVME_EXCLUSIVE_ACCESS_ALL_REGISTRANTS=6
};

//acquire actions
enum NVMEPRDev_Acquire_Actions { 
    NVME_RACQA_ACQUIRE=0,
    NVME_RACQA_PREEMPT=1,
    NVME_RACQA_PREEMPT_AND_ABORT=2
};

//acquire actions
enum NVMEPRDev_Release_Actions { 
    NVME_RRELA_RELEASE=0,
    NVME_RRELA_CLEAR=1
};

struct persistent_reserve_device* nvme_pr_device_init(const char* device_name, const char *key_path);
void nvme_pr_device_handle_delete(struct persistent_reserve_device* device);

#ifdef __cplusplus
}
#endif


#endif
