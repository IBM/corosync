#include "nvme_device.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct device_priv {
	struct persistent_reserve_device super;
	uint64_t ikey;
	char key[PERSISTENT_RESERVE_KEY_LENGTH + 1];
	char device_name[PERSISTENT_RESERVE_DEVICE_MAXLEN + 1];
	char wwn[PERSISTENT_RESERVE_DEVICE_MAXLEN+1];
};

struct resv_rep_controller {
	uint64_t hostid;
	uint64_t ikey;
	char key[NVMEPRDEV_KEY_LENGTH + 1];
	uint8_t rcsts; ///< Reservation Status (bit 0 is one if this controler holds a reservation)
	uint16_t id;
};

struct resv_report {
	uint32_t gen;
	uint8_t rtype;
	uint8_t ptpls; ///< Persist Through Power Loss State
	uint16_t num_controllers;

	struct resv_rep_controller controllers[];
};

static pr_dev_err get_reservation_report(struct device_priv *device, struct resv_report **report)
{
	char cmd[CMD_LENGTH] = { 0 };
	char line[CMD_RESULT_LENGTH] = { 0 };
	FILE *pipe = NULL;

	uint32_t gen = 0;
	uint8_t rtype = 0;
	uint8_t ptpls = 0;
	uint16_t num_controllers = 0;

	struct resv_report *res = NULL;

	sprintf(cmd, "nvme resv-report %s", device->device_name);

	pipe = popen(cmd, "r");
	if(!pipe) {
		fprintf(stderr, "Error opening pipe to run command '%s'", cmd);
		return PR_ERR_UNKNOWN; // FIXME: use a better error
	}

	// TODO: improve checking

	// Parse headers
	while(fgets(line, CMD_RESULT_LENGTH, pipe) != NULL) {
		uintmax_t value;
		char *split = strchr(line, ':');
		if(!split) continue; // blank line or something we don't care about

		value = strtoumax(split + 1, NULL, 10);

		if(strstr(line, "regctlext")) {
			break;
		} else if(strstr(line, "gen")) {
			gen = (uint32_t)value;
		} else if(strstr(line, "rtype")) {
			rtype = (uint8_t)value;
		} else if(strstr(line, "regctl")) {
			num_controllers = (uint16_t)value;
		} else if(strstr(line, "ptpls")) {
			ptpls = (uint8_t)value;
		}
	}

	res = calloc(1, sizeof(*res) + num_controllers * sizeof(res->controllers[0]));

	if(!res) {
		pclose(pipe);
		return PR_ERR_UNKNOWN; // FIXME: use a better error
	}

	res->gen = gen;
	res->rtype = rtype;
	res->ptpls = ptpls;
	res->num_controllers = num_controllers;

	for(size_t i = 0; i < num_controllers; i++) {
		do {
			uintmax_t value;
			char *split = NULL;
			if(strncmp(line, "regctl", 6) == 0) break;

			split = strchr(line, ':');
			if(!split) continue;
			*split = '\0';

			value = strtoumax(split + 1, NULL, 16);

			if(strstr(line, "cntlid")) {
				res->controllers[i].id = (uint16_t)value;
			} else if(strstr(line, "rcsts")) {
				res->controllers[i].rcsts = (uint8_t)value;
			} else if(strstr(line, "rkey")) {
				int count = 0;
				char *c = split + 1;
				for(; *c != '\0' && isspace(c); c++) {
				}
				count = sscanf(c, "%" SCNx64, &res->controllers[i].ikey);
				if(count < 0) {
					free(res);
					pclose(pipe);
					return PR_ERR_UNKNOWN; // TODO: better error
				}
				c += count;
				sprintf(res->controllers[i].key, "%" PERSISTENT_RESERVE_PRI_KEY , res->controllers[i].ikey); // Can't run past buffer, always fixed size
			} else if(strstr(line, "hostid")) {
				res->controllers[i].hostid = (uint64_t)value;
			}
		} while(fgets(line, CMD_RESULT_LENGTH, pipe) != NULL);
	}

	pclose(pipe);
	*report = res;
	return PR_ERR_OK;
}

static pr_dev_err device_get_registered_keys(struct persistent_reserve_device *handle, size_t *num_keys, uint64_t **keys)
{
	struct device_priv *nvme_device = (struct device_priv *)handle;
	pr_dev_err ret = 0;
	struct resv_report *report = NULL;

	ret = get_reservation_report(nvme_device, &report);
	if(ret != PR_ERR_OK) {
		return ret;
	}

	*num_keys = report->num_controllers;
	*keys = calloc(report->num_controllers, sizeof(**keys));
	for(size_t i = 0; i < report->num_controllers; i++) {
		(*keys)[i] = report->controllers[i].ikey;
	}
	free(report);
	return 0;
}

static pr_dev_err device_register_key(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_handle = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = { 0 };
	char result[CMD_RESULT_LENGTH] = { 0 };
	int ret = 0;

	sprintf(cmd, "nvme resv-register %s --crkey=0x0 --nrkey=0x%" PERSISTENT_RESERVE_PRI_KEY " --rrega=0 2>&1 > /dev/null",
	        nvme_handle->device_name, nvme_handle->ikey);
	ret = exec_cmd(cmd, result);
#ifdef DEBUG
	showCommand(cmd, result, ret);
#endif
	return ret;
}

static pr_dev_err device_unregister_key(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_handle = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = {};
	char result[CMD_RESULT_LENGTH];
	int ret = 0;

	sprintf(cmd, "nvme resv-register %s --crkey=0x%" PERSISTENT_RESERVE_PRI_KEY " --nrkey=0x0 --rrega=1 2>&1 > /dev/null", nvme_handle->device_name,
	        nvme_handle->ikey);
	ret = exec_cmd(cmd, result);
#ifdef DEBUG
	showCommand(cmd, result, ret);
#endif
	return ret;
}

static pr_dev_err device_reserve(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_handle = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = { 0 };
	char result[CMD_RESULT_LENGTH] = { 0 };
	int ret = 0;

	sprintf(cmd, "nvme resv-acquire %s --crkey=0x%" PERSISTENT_RESERVE_PRI_KEY " --racqa=0 --rtype=%d 2>&1 > /dev/null", nvme_handle->device_name,
	        nvme_handle->ikey, NVME_WRITE_EXCLUSIVE_REGISTRANTS_ONLY);
	ret = exec_cmd(cmd, result);
#ifdef DEBUG
	showCommand(cmd, result, ret);
#endif
	return ret;
}

static pr_dev_err device_release(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_handle = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = {};
	char result[CMD_RESULT_LENGTH];
	int ret = 0;

	sprintf(cmd, "nvme resv-release %s --crkey=0x%" PERSISTENT_RESERVE_PRI_KEY " --rtype=%d --rrela=%d 2>&1 > /dev/null", nvme_handle->device_name,
	        nvme_handle->ikey, NVME_WRITE_EXCLUSIVE_REGISTRANTS_ONLY, NVME_RRELA_RELEASE);
	ret = exec_cmd(cmd, result);
#ifdef DEBUG
	showCommand(cmd, result, ret);
#endif
	return ret;
}

static pr_dev_err device_abort(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_handle = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = { 0 };
	char result[CMD_RESULT_LENGTH] = { 0 };
	int ret = 0;

	sprintf(cmd, "nvme resv-release %s --crkey=0x%" PERSISTENT_RESERVE_PRI_KEY " --rtype=%d --rrela=%d 2>&1 > /dev/null", nvme_handle->device_name,
	        nvme_handle->ikey, NVME_WRITE_EXCLUSIVE_REGISTRANTS_ONLY, NVME_RRELA_CLEAR);
	ret = exec_cmd(cmd, result);
	if(ret != 0) {
		return PR_ERR_REGISTRATION_FAILED;
	}
#ifdef DEBUG
	showCommand(cmd, result, ret);
#endif
	return PR_ERR_OK;
}

static int device_is_reserved(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_device = (struct device_priv *)handle;
	pr_dev_err ret = 0;

	struct resv_report *report = NULL;

	ret = get_reservation_report(nvme_device, &report);
	if(ret != PR_ERR_OK) {
		return ret;
	}

	for(size_t i = 0; i < report->num_controllers; i++) {
		if(report->controllers[i].rcsts) {
			free(report);
			return 1;
		}
	}
	free(report);
	return 0;
}

static const char *device_get_name(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_device = (struct device_priv *)handle;
	return nvme_device->device_name;
}

static const char *device_get_key(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_device = (struct device_priv *)handle;
	return nvme_device->key;
}

static uint64_t device_get_ikey(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_device = (struct device_priv *)handle;
	return nvme_device->ikey;
}

static const char *device_get_id(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	return device->wwn;
}

static pr_dev_err device_get_reservation_owner_key(struct persistent_reserve_device *handle, uint64_t *key)
{
	
}

static const struct persistent_reserve_device_class nvme_device_vtable = { 
	.register_key = device_register_key,
	.unregister_key = device_unregister_key,
	.reserve = device_reserve,
	.release = device_release,
	.is_reserved = device_is_reserved,
	.abort = device_abort,
	.get_name = device_get_name,
	.get_key = device_get_key,
	.get_ikey = device_get_ikey,
	.get_registered_keys = device_get_registered_keys,
	.get_device_id = device_get_id,
	.get_reservation_owner_key = device_get_reservation_owner_key,
};

struct persistent_reserve_device *nvme_pr_device_init(const char *device_name, const char *key_path)
{
	char cmd[CMD_LENGTH] = {};
	FILE *pipe = NULL;
	struct device_priv *device = NULL;

	if(strlen(device_name) >= PERSISTENT_RESERVE_DEVICE_MAXLEN) {
		return NULL;
	}

	device = malloc(sizeof(*device));
	if(NULL == device) {
		return NULL;
	}
	device->super.clazz = &nvme_device_vtable;
	strncpy(device->device_name, device_name, PERSISTENT_RESERVE_DEVICE_MAXLEN);

	// if( PR_ERR_OK != get_reserve_key(key_path, device->key, NVMEPRDEV_KEY_LENGTH) )
	// {
	//     free(device);
	//     return NULL;
	// }

	if(access(key_path, F_OK) != 0) {
		printf("Generating key\n");
		char key[PERSISTENT_RESERVE_KEY_LENGTH + 1] = { 0 };
		generate_reserve_key(key_path, key, PERSISTENT_RESERVE_KEY_LENGTH);
	}

	FILE *keyfile = fopen(key_path, "ro");
	fscanf(keyfile, "%" SCNx64, &device->ikey);
	fclose(keyfile);
	sprintf(device->key, "%" PERSISTENT_RESERVE_PRI_KEY, device->ikey);

	sprintf(cmd, "lsblk --nodeps --noheadings --output WWN %s", device->device_name);
	pipe = popen(cmd, "r");
	if(!pipe) {
		fprintf(stderr, "Error opening pipe to run command '%s'", cmd);
		return NULL;
	}
	if(fgets(device->wwn, sizeof(device->wwn), pipe) == NULL) {
		pclose(pipe);
		return NULL;
	}
	pclose(pipe);

	return (struct persistent_reserve_device *)device;
}

void nvme_pr_device_handle_delete(struct persistent_reserve_device *device)
{
	free(device);
}

int is_nvme_device(const char *device)
{
	int ret = 0;
	char cmd[CMD_LENGTH] = {};
	char result[CMD_RESULT_LENGTH];

	sprintf(cmd, "nvme id-ctrl %s 2>&1 1>/dev/null", device);
	ret = exec_cmd(cmd, result);
#ifdef DEBUG
	printf("cmd:%s\nrc:%d\n%s\n", cmd, ret, result);
#endif

	return ret == 0;
}
