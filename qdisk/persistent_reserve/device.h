#ifndef PERSISTENT_RESERVE_DEVICE_H
#define PERSISTENT_RESERVE_DEVICE_H

#include <unistd.h>
#include <stddef.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PERSISTENT_RESERVE_DEVICE_MAXLEN 300U
#define PERSISTENT_RESERVE_KEY_LENGTH 16
#define PERSISTENT_RESERVE_PRI_KEY "016" PRIx64

#define CMD_LENGTH 512
#define CMD_RESULT_LENGTH 8192

typedef enum {    
	PR_ERR_OK = 0,
	PR_ERR_UNKNOWN = -127,
	PR_ERR_DEVICE_LEN_EXCEEDED,
	PR_ERR_DEVICE_INVALID_DEVICE_TYPE,
	PR_ERR_DEVICE_MEMORY_ALLOCATION_FAILED,
	PR_ERR_DEVICE_CANT_OPEN_KEYFILE,
	PR_ERR_DEVICE_CANT_READ_KEYFILE,
	PR_ERR_DEVICE_NOT_READY,
	PR_ERR_DEVICE_NOT_OPERABLE,
	PR_ERR_REGISTRATION_FAILED,
	PR_ERR_RESERVATION_FAILED,
	PR_ERR_UNREGISTER_FAILED,
	PR_ERR_RELEASE_FAILED,
	PR_ERR_ABORT_FAILED,
	PR_ERR_FOUND_UNREGISTERED_NODE_KEY,
	PR_ERR_EXEC_FAILED,

} pr_dev_err;

struct persistent_reserve_device_class;

struct persistent_reserve_device
{
	const struct persistent_reserve_device_class *clazz;
};

struct persistent_reserve_device_class {
	pr_dev_err (*register_key)(struct persistent_reserve_device *handle);
	pr_dev_err (*unregister_key)(struct persistent_reserve_device *handle);
	pr_dev_err (*reserve)(struct persistent_reserve_device *handle);
	pr_dev_err (*release)(struct persistent_reserve_device *handle);
	int (*is_reserved)(struct persistent_reserve_device *handle);
	const char* (*get_name)(struct persistent_reserve_device *handle);
	const char* (*get_key)(struct persistent_reserve_device *handle);
	uint64_t (*get_ikey)(struct persistent_reserve_device *handle);
	pr_dev_err (*get_registered_keys)(struct persistent_reserve_device *handle, size_t *num_keys, uint64_t **keys);
	pr_dev_err (*abort)(struct persistent_reserve_device *handle);
	const char * (*get_device_id)(struct persistent_reserve_device *);
	pr_dev_err (*get_reservation_owner_key)(struct persistent_reserve_device *handle, uint64_t *key);
};

static inline pr_dev_err persistent_reserve_device_register_key(struct persistent_reserve_device *handle) { return handle->clazz->register_key(handle); }
static inline pr_dev_err persistent_reserve_device_unregister_key(struct persistent_reserve_device *handle) { return handle->clazz->unregister_key(handle); }
static inline pr_dev_err persistent_reserve_device_reserve(struct persistent_reserve_device *handle) { return handle->clazz->reserve(handle); }
static inline pr_dev_err persistent_reserve_device_release(struct persistent_reserve_device *handle) { return handle->clazz->release(handle); }

/**
 * Check if the device is already reserved
 * @return negative for error, 0 for false, positive for true
 */
static inline int persistent_reserve_device_is_reserved(struct persistent_reserve_device *handle) { return handle->clazz->is_reserved(handle); }
static inline const char* persistent_reserve_device_get_name(struct persistent_reserve_device *handle) { return handle->clazz->get_name(handle); }
static inline const char* persistent_reserve_device_get_key(struct persistent_reserve_device *handle) { return handle->clazz->get_key(handle); }
static inline uint64_t persistent_reserve_device_get_ikey(struct persistent_reserve_device *handle) { return handle->clazz->get_ikey(handle); }
static inline const char* persistent_reserve_device_get_device_id(struct persistent_reserve_device *handle) { return handle->clazz->get_device_id(handle); }

/**
 * @brief 
 * @param handle
 * @param num_keys [out]
 * @param keys [out]
 */
static inline pr_dev_err persistent_reserve_device_get_registered_keys(struct persistent_reserve_device *handle, size_t *num_keys, uint64_t **keys) { return handle->clazz->get_registered_keys(handle, num_keys, keys); }


/**
 * @brief Get the key registered by the current holder of a reservation
 * @param handle device handle of a persistant reserver device
 * @param key [out] pointer to a uint64_t to store the key in. Set to 0 if the device is not reserved
 * @return Error code or PR_DEV_OK on success
 */
static inline pr_dev_err persistent_reserve_device_reservation_owner_key(struct persistent_reserve_device *handle, uint64_t *key) { return handle->clazz->get_reservation_owner_key(handle, key); }

static inline pr_dev_err persistent_reserve_device_abort(struct persistent_reserve_device *handle) { return handle->clazz->abort(handle); }

int is_scsi_device(const char* device);
int is_nvme_device(const char* device);

// TODO: add function for checking if WE hold the reservation

struct persistent_reserve_device *persistent_reserve_device_new(const char *device_path, const char *key_path);


pr_dev_err get_reserve_key(const char *key_file, char *key, size_t key_length);
pr_dev_err generate_reserve_key(const char *key_file, char *key, size_t key_length);
pr_dev_err read_reserve_key(const char *key_file, char *key, size_t key_length);

int exec_cmd(const char* cmd, char* result);

#ifdef __cplusplus
}
#endif

#endif 
