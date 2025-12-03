#include "scsi_device.h"

#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <stdint.h>

enum sg3_err {
	SG3_OK,
	
};

struct device_priv {
	struct persistent_reserve_device super;
	uint64_t ikey;
	char key[PERSISTENT_RESERVE_KEY_LENGTH + 1];
	char device_name[PERSISTENT_RESERVE_DEVICE_MAXLEN+1];
	char wwn[PERSISTENT_RESERVE_DEVICE_MAXLEN+1];
};

struct device_reservation
{
	uint32_t generation;
	bool reserved;
	uint64_t key;
};

static pr_dev_err read_reservation(struct device_priv *device, struct device_reservation *res);

static pr_dev_err device_register_key(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = {};
	char result[CMD_RESULT_LENGTH] = {};
	int ret = 0;

	//TODO: Need to check for existing registered keys and error out
	
	// sprintf(cmd, "sg_persist --out --register --param-sark=0x%" PERSISTENT_RESERVE_PRI_KEY " %s 2>&1 > /dev/null", device->ikey, device->device_name);
	sprintf(cmd, "sg_persist --out --register-ignore --param-sark=0x%" PERSISTENT_RESERVE_PRI_KEY " %s 2>&1 > /dev/null", device->ikey, device->device_name);
	ret = exec_cmd(cmd, result);
#ifdef DEBUG
	printf("cmd:%s\nrc:%d\n%s\n", cmd, ret, result);
#endif
	if(ret == 0) {
		return PR_ERR_OK;
	} else {
		return PR_ERR_REGISTRATION_FAILED;
	}
}

static pr_dev_err device_unregister_key(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = { 0 };
	char result[CMD_RESULT_LENGTH] = { 0 };
	int ret = 0;

	sprintf(cmd, "sg_persist --out --register --param-rk=0x%" PERSISTENT_RESERVE_PRI_KEY " --param-sark=0x0 %s 2>&1 > /dev/null", device->ikey, device->device_name);
	ret = exec_cmd(cmd, result);
#ifdef DEBUG
	printf("cmd:%s\nrc:%d\n%s\n", cmd, ret, result);
#endif
	if(ret == 0) {
		return PR_ERR_OK;
	} else {
		return PR_ERR_UNREGISTER_FAILED;
	}
}

static pr_dev_err device_reserve(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = {};
	char result[CMD_RESULT_LENGTH] = {};
	int ret = 0;

	sprintf(cmd, "sg_persist --out --reserve --param-rk=0x%" PERSISTENT_RESERVE_PRI_KEY " --prout-type=%d %s 2>&1 > /dev/null", device->ikey,
	        SCSI_WRITE_EXCLUSIVE_REGISTRANTS_ONLY, device->device_name);
	ret = exec_cmd(cmd, result);
#ifdef DEBUG
	printf("cmd:%s\nrc:%d\n%s\n", cmd, ret, result);
#endif
	if(ret == 0) {
		return PR_ERR_OK;
	} else {
		return PR_ERR_RESERVATION_FAILED;
	}
}

static pr_dev_err device_release(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = {};
	char result[CMD_RESULT_LENGTH] = {};
	int ret = 0;
	sprintf(cmd, "sg_persist --out --release --param-rk=0x%" PERSISTENT_RESERVE_PRI_KEY " --prout-type=%d %s 2>&1 > /dev/null", device->ikey,
	        SCSI_WRITE_EXCLUSIVE_REGISTRANTS_ONLY, device->device_name);
	ret = exec_cmd(cmd, result);
#ifdef DEBUG
	printf("cmd:%s\nrc:%d\n%s\n", cmd, ret, result);
#endif
	if(ret == 0) {
		return PR_ERR_OK;
	} else {
		return PR_ERR_RELEASE_FAILED;
	}
}

static int device_is_reserved(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	struct device_reservation res;
	int ret = read_reservation(device, &res);

	if(!ret) return res.reserved;

	return ret;
}

static const char *device_get_name(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	return device->device_name;
}

static const char *device_get_key(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	return device->key;
}

static uint64_t device_get_ikey(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	return device->ikey;
}

static pr_dev_err device_abort(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = {};
	char result[CMD_RESULT_LENGTH] = {};
	int ret = 0;
	uint64_t cur_reservation_key = 0;

	// get the existing reservation key and remove any newline / return characters
	sprintf(cmd, "sg_persist --in --read-reservation %s | grep Key | awk -F= '{print$2}'", device->device_name);
	ret = exec_cmd(cmd, result);
	if(ret != 0) {
		return PR_ERR_ABORT_FAILED;
	}
#ifdef DEBUG
	printf("cmd:%s\nrc:%d\n%s\n", cmd, ret, currentReservationKey);
#endif

	sscanf(result, "%" SCNx64, &cur_reservation_key);

	// preemptively make the reservation with this host
	snprintf(cmd, sizeof(cmd), "sg_persist --out --preempt-abort --param-rk=0x%" PERSISTENT_RESERVE_PRI_KEY " --param-sark=0x%" PERSISTENT_RESERVE_PRI_KEY " --prout-type=%d %s 2>&1 > /dev/null",
	         device->ikey, cur_reservation_key, SCSI_WRITE_EXCLUSIVE_REGISTRANTS_ONLY, device->device_name);
	ret = exec_cmd(cmd, result);
	if(ret != 0) {
		return PR_ERR_ABORT_FAILED;
	}
#ifdef DEBUG
	printf("cmd:%s\nrc:%d\n%s\n", cmd, ret, result);
#endif

	device_release(handle);
	return PR_ERR_OK;
}

static pr_dev_err device_get_registered_keys(struct persistent_reserve_device *handle, size_t *num_keys, uint64_t **res)
{
	struct device_priv *device = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = {};
	char line[CMD_RESULT_LENGTH] = {};
	FILE *pipe = NULL;
	size_t cur_max_keys = 8;
	uint64_t *keys = NULL;
	size_t i = 0;
	pr_dev_err err = 0;

	keys = calloc(cur_max_keys, sizeof(*keys));
	if(!keys) {
		return PR_ERR_DEVICE_MEMORY_ALLOCATION_FAILED;
	}

	// get the existing reservation key and remove any newline / return characters
	sprintf(cmd, "sg_persist --in --read-full-status %s | grep Key | awk -F= '{print$2}'", device->device_name);
	pipe = popen(cmd, "r");
	if(!pipe) {
		fprintf(stderr, "Error opening pipe to run command '%s'\n", cmd);
		free(keys);
		return PR_ERR_UNKNOWN; // FIXME: use a better error
	}

	while(fgets(line, sizeof(line), pipe) != NULL) {
		if(i >= cur_max_keys) {
			uint64_t *key_temp = NULL;
			cur_max_keys *= 2;
			key_temp = realloc(keys, cur_max_keys * sizeof(*keys));
			if(key_temp == NULL) {
				free(keys);
				pclose(pipe);
				return PR_ERR_DEVICE_MEMORY_ALLOCATION_FAILED;
			}
		}

		// ignore any blank or malformed lines just in case
		if(sscanf(line, "%" SCNx64, keys + i) > 0) {
			++i;
		}
	}

	pclose(pipe);

	*num_keys = i;
	*res = keys;
	return err;
}

static const char *device_get_id(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	return device->wwn;
}

static pr_dev_err device_get_reservation_owner_key(struct persistent_reserve_device *handle, uint64_t *key)
{
	struct device_priv *device = (struct device_priv *)handle;
	struct device_reservation res;
	int ret = read_reservation(device, &res);

	if(!ret && res.reserved) {
		*key = res.key;
	}
	
	return ret;
}

static struct persistent_reserve_device_class scsi_device_vtable = {
	.register_key = device_register_key,
	.unregister_key = device_unregister_key,
	.reserve = device_reserve,
	.release = device_release,
	.is_reserved = device_is_reserved,
	.get_name = device_get_name,
	.get_key = device_get_key,
	.get_ikey = device_get_ikey,
	.get_registered_keys = device_get_registered_keys,
	.abort = device_abort,
	.get_device_id = device_get_id,
	.get_reservation_owner_key = device_get_reservation_owner_key,
};

struct persistent_reserve_device *scsi_pr_device_init(const char *device_name, const char *key_path)
{
	struct device_priv *device = NULL;
	FILE *keyfile = NULL;

	char cmd[CMD_LENGTH] = {};
	FILE *pipe = NULL;

	if(strlen(device_name) >= PERSISTENT_RESERVE_DEVICE_MAXLEN) {
		return NULL;
	}

	device = malloc(sizeof(*device));
	if(!device) {
		return NULL;
	}
	device->super.clazz = &scsi_device_vtable;
	strncpy(device->device_name, device_name, PERSISTENT_RESERVE_DEVICE_MAXLEN);

	// if(PR_ERR_OK != get_reserve_key(key_path, device->key, PERSISTENT_RESERVE_KEY_LENGTH)) {
	// 	free(device);
	// 	return NULL;
	// }
	
	if(access(key_path, F_OK) != 0) {
		char key[PERSISTENT_RESERVE_KEY_LENGTH + 1] = { 0 };
		printf("Generating key\n");
		generate_reserve_key(key_path, key, PERSISTENT_RESERVE_KEY_LENGTH);
	}

	keyfile = fopen(key_path, "ro");
	fscanf(keyfile, "%" SCNx64, &device->ikey);
	fclose(keyfile);
	sprintf(device->key, "%" PERSISTENT_RESERVE_PRI_KEY, device->ikey);

	
	sprintf(cmd, "lsblk --nodeps --noheadings --output WWN %s", device->device_name);
	pipe = popen(cmd, "r");
	if(!pipe) {
		fprintf(stderr, "Error opening pipe to run command '%s'\n", cmd);
		return NULL;
	}
	if(fgets(device->wwn, sizeof(device->wwn), pipe) == NULL) {
		pclose(pipe);
		return NULL;
	}
	pclose(pipe);

	return (struct persistent_reserve_device *)device;
}

void scsi_pr_device_delete(struct persistent_reserve_device *device)
{
	free(device);
}


int is_scsi_device(const char *device)
{
	int ret = 0;
	char cmd[CMD_LENGTH] = {};
	char result[CMD_RESULT_LENGTH];

	sprintf(cmd, "sg_verify %s 2>&1 1>/dev/null", device);
	ret = exec_cmd(cmd, result);
#ifdef DEBUG
	printf("cmd:%s\nrc:%d\n%s\n", cmd, ret, result);
#endif

	return ret == 0;
}


static pr_dev_err read_reservation(struct device_priv *device, struct device_reservation *res)
{
	char cmd[CMD_LENGTH] = {};
	char line[CMD_RESULT_LENGTH] = {};
	FILE *pipe = NULL;
	int cerr = 0;

	/*
	LIO-ORG   disk1             4.0 
	Peripheral device type: disk
	PR generation=0x6c, Reservation follows:
	  Key=0xea1b2a62b1dd8f34
	  scope: LU_SCOPE,  type: Write Exclusive, registrants only
	
	LIO-ORG   disk1             4.0 
	Peripheral device type: disk
	PR generation=0x6c, there is NO reservation held
	
	*/

	sprintf(cmd, "sg_persist --in --read-reservation %s", device->device_name);
	pipe = popen(cmd, "r");
	if(!pipe) {
		fprintf(stderr, "Error opening pipe to run command '%s'\n", cmd);
		return PR_ERR_UNKNOWN; // FIXME: use a better error
	}

	// Look for line with reservation status
	while(1) {
		int scr = 0;
		res->generation = 0;
		res->reserved = false;
		res->key = 0;

		if(fgets(line, sizeof(line), pipe) == NULL) {
			goto err;
		}

		scr = sscanf(line, " PR generation=%" SCNx32, &res->generation);
		if(scr == 1) {
			if(strstr(line, "Reservation follows")) {
				res->reserved = true;
				break; // Matched the line
			} else {
				// Don't need any of the other info, no reservation
				pclose(pipe);
				return PR_ERR_OK;
			}
		}
	}

	// Look for Key=0x... line
	while(1) {
		int scr = 0;

		if(fgets(line, sizeof(line), pipe) == NULL) {
			goto err;
		}

		scr = sscanf(line, " Key=%" SCNx64, &res->key);
		if(scr == 1) {
			break; // Matched the line
		}
	}

	// Could look for next line and extract info from it, but we currently don't need anything it says.
	pclose(pipe);

	return PR_ERR_OK;

err:
	if(feof(pipe) || ferror(pipe)) {
		cerr = pclose(pipe);
		if(cerr) {
			fprintf(stderr, "Unexepected end of stream while running '%s'\n", cmd);
			return PR_ERR_DEVICE_NOT_OPERABLE; //TODO: try to map error code
		}
	}

	fprintf(stderr, "Unknown error running %s\n", cmd);

	return PR_ERR_UNKNOWN;
}
