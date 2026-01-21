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

#include "persistent_reserve.h"
#include "scsi_device.h"
#include "helpers.h"

#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <stdint.h>
#include <syslog.h>

//reservation types
enum scsi_pr_device_reservation_type { 
    SCSI_WRITE_EXCLUSIVE=1,
    SCSI_EXCLUSIVE_ACCESS=3,
    SCSI_WRITE_EXCLUSIVE_REGISTRANTS_ONLY=5,
    SCSI_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY=6,
    SCSI_WRITE_EXCLUSIVE_ALL_REGISTRANTS=7,
    SCSI_EXCLUSIVE_ACCESS_ALL_REGISTRANTS=8
};

struct device_priv {
	struct persistent_reserve_device super;
	uint64_t ikey;
	char key[PERSISTENT_RESERVE_KEY_LENGTH + 1];
	char device_name[PERSISTENT_RESERVE_DEVICE_MAXLEN+1];
	char id[PERSISTENT_RESERVE_DEVICE_MAXLEN+1];
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
	char cmd[CMD_LENGTH + 1] = { 0 };
	int ret = 0;

	//TODO: Need to check for existing registered keys and error out

	snprintf(cmd, CMD_LENGTH, "sg_persist --out --register-ignore --param-sark=0x%" PERSISTENT_RESERVE_PRI_KEY " %s 2>&1 > /dev/null", device->ikey, device->device_name);
	ret = exec_cmd(cmd);
	if(ret == 0) {
		return PR_ERR_OK;
	} else {
		return PR_ERR_REGISTRATION_FAILED;
	}
}

static pr_dev_err device_unregister_key(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	char cmd[CMD_LENGTH+1] = { 0 };
	int ret = 0;

	snprintf(cmd, CMD_LENGTH, "sg_persist --out --register --param-rk=0x%" PERSISTENT_RESERVE_PRI_KEY " --param-sark=0x0 %s 2>&1 > /dev/null",
	         device->ikey, device->device_name);
	ret = exec_cmd(cmd);
	if(ret == 0) {
		return PR_ERR_OK;
	} else {
		return PR_ERR_UNREGISTER_FAILED;
	}
}

static pr_dev_err device_reserve(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	char cmd[CMD_LENGTH+1] = { 0 };
	int ret = 0;

	snprintf(cmd, CMD_LENGTH, "sg_persist --out --reserve --param-rk=0x%" PERSISTENT_RESERVE_PRI_KEY " --prout-type=%d %s 2>&1 > /dev/null",
	         device->ikey, SCSI_WRITE_EXCLUSIVE_REGISTRANTS_ONLY, device->device_name);
	ret = exec_cmd(cmd);
	if(ret == 0) {
		return PR_ERR_OK;
	} else {
		return PR_ERR_RESERVATION_FAILED;
	}
}

static pr_dev_err device_release(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	char cmd[CMD_LENGTH + 1] = { 0 };
	int ret = 0;
	snprintf(cmd, CMD_LENGTH, "sg_persist --out --release --param-rk=0x%" PERSISTENT_RESERVE_PRI_KEY " --prout-type=%d %s 2>&1 > /dev/null",
	         device->ikey, SCSI_WRITE_EXCLUSIVE_REGISTRANTS_ONLY, device->device_name);
	ret = exec_cmd(cmd);
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
	char cmd[CMD_LENGTH + 1] = { 0 };
	char result[CMD_RESULT_LENGTH] = { 0 };
	int ret = 0;
	uint64_t cur_reservation_key = 0;

	// get the existing reservation key and remove any newline / return characters
	snprintf(cmd, CMD_LENGTH, "sg_persist --in --read-reservation %s | grep Key | awk -F= '{print$2}'", device->device_name);
	ret = exec_cmd_output(cmd, result, sizeof(result));
	if(ret != 0) {
		return PR_ERR_ABORT_FAILED;
	}

	sscanf(result, "%" SCNx64, &cur_reservation_key);

	// preemptively make the reservation with this host
	snprintf(cmd, CMD_LENGTH, "sg_persist --out --preempt-abort --param-rk=0x%" PERSISTENT_RESERVE_PRI_KEY " --param-sark=0x%" PERSISTENT_RESERVE_PRI_KEY " --prout-type=%d %s 2>&1 > /dev/null",
	         device->ikey, cur_reservation_key, SCSI_WRITE_EXCLUSIVE_REGISTRANTS_ONLY, device->device_name);
	ret = exec_cmd(cmd);
	if(ret != 0) {
		return PR_ERR_ABORT_FAILED;
	}

	device_release(handle);
	return PR_ERR_OK;
}

static pr_dev_err device_get_registered_keys(struct persistent_reserve_device *handle, size_t *num_keys, uint64_t **res)
{
	struct device_priv *device = (struct device_priv *)handle;
	char cmd[CMD_LENGTH + 1] = { 0 };
	char line[CMD_RESULT_LENGTH] = { 0 };
	FILE *pipe = NULL;
	size_t cur_max_keys = 8;
	uint64_t *keys = NULL;
	size_t i = 0;

	keys = calloc(cur_max_keys, sizeof(*keys));
	if(!keys) {
		return PR_ERR_DEVICE_MEMORY_ALLOCATION_FAILED;
	}

	// get the existing reservation key and remove any newline / return characters
	snprintf(cmd, CMD_LENGTH, "sg_persist --in --read-full-status %s | grep Key | awk -F= '{print$2}'", device->device_name);
	pipe = popen(cmd, "r");
	if(!pipe) {
		pr_log(LOG_ERR, "Error opening pipe to run command '%s'\n", cmd);
		free(keys);
		return PR_ERR_EXEC_FAILED;
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
			keys = key_temp;
		}

		// ignore any blank or malformed lines just in case
		if(sscanf(line, "%" SCNx64, keys + i) > 0) {
			++i;
		}
	}

	pclose(pipe);

	*num_keys = i;
	*res = keys;
	return PR_ERR_OK;
}

static const char *device_get_id(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	return device->id;
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

static void device_delete(struct persistent_reserve_device *device)
{
	free(device);
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
	.free = device_delete,
};

static char *map_device_path(const char *device_name)
{
	if(strncmp(device_name, "wwn:", 4) == 0) {
		char tmp[PERSISTENT_RESERVE_DEVICE_MAXLEN + 1] = { 0 };
		strncpy(tmp, device_name+4, PERSISTENT_RESERVE_DEVICE_MAXLEN);
		// trim spaces
		size_t skip = strspn(tmp, " \t\n");
		tmp[skip + strcspn(tmp+skip, " \t\n")] = '\0';

		char *res = calloc(sizeof(char), PERSISTENT_RESERVE_DEVICE_MAXLEN + 1);
		snprintf(res, PERSISTENT_RESERVE_DEVICE_MAXLEN, "/dev/disk/by-id/wwn-0x%s", tmp + skip);
		return res;
	}

	return strdup(device_name);
}

struct persistent_reserve_device *scsi_pr_device_init(const char *device_name, uint64_t ikey)
{
	struct device_priv *device = NULL;

	char cmd[CMD_LENGTH + 1] = { 0 };
	FILE *pipe = NULL;

	if(strlen(device_name) >= PERSISTENT_RESERVE_DEVICE_MAXLEN) {
		return NULL;
	}

	device = malloc(sizeof(*device));
	if(!device) {
		return NULL;
	}
	device->super.clazz = &scsi_device_vtable;
	char *tmp = map_device_path(device_name);
	strncpy(device->device_name, tmp, PERSISTENT_RESERVE_DEVICE_MAXLEN);
	device->device_name[PERSISTENT_RESERVE_DEVICE_MAXLEN] = '\0';
	free(tmp); tmp = NULL;

	device->ikey = ikey;
	sprintf(device->key, "%" PERSISTENT_RESERVE_PRI_KEY, device->ikey); // cannot overrun, always produces same length string

	snprintf(cmd, CMD_LENGTH, "lsblk --nodeps --noheadings --output WWN %s", device->device_name);
	pipe = popen(cmd, "r");
	if(!pipe) {
		pr_log(LOG_ERR, "Error opening pipe to run command '%s'\n", cmd);
		free(device);
		return NULL;
	}
	if(fgets(device->id, sizeof(device->id), pipe) == NULL) {
		pclose(pipe);
		free(device);
		return NULL;
	}
	pclose(pipe);

	return (struct persistent_reserve_device *)device;
}

int is_scsi_device(const char *device)
{
	char cmd[CMD_LENGTH] = {};

	char *device_path = map_device_path(device);
	sprintf(cmd, "sg_verify --readonly %s 2>&1 1>/dev/null", device_path);
	free(device_path);

	// Seem to sometimes get spurious errors, retry at least a few times
	for(int i=0; i<3; i++) {
		if(exec_cmd(cmd) == 0) {
			return true;
		}
		sleep(1);
	}

	return exec_cmd(cmd) == 0;
}

int is_scsi_device_usable(const char* device)
{
	char cmd[CMD_LENGTH] = {};
	char result[CMD_RESULT_LENGTH];
	char *device_path = map_device_path(device);
	sprintf(cmd, "sg_persist --readonly --in --report-capabilities %s", device_path);
	free(device_path);

	for(int i=0; i < 5; i++) {
		int ret = exec_cmd_output(cmd, result, sizeof(result));

		// 11     command aborted, retry
		// 33     the command sent to DEVICE has timed out.
		if(ret == 11 || ret == 33) {
			continue;
		}

		// 5 -> illegal request, can just assume no support
		// 9 -> ditto
		// 15     the utility is unable to open, close or use the given DEVICE or some other file
		// 36     no error has occurred plus the utility wants to convey a boolean value of false.
		//  126    the utility was found but could not be executed.
		if(ret == 5 || ret == 9 || ret == 15 || ret == 36 || ret == 126) {
			return 0;
		}

		// 21     the DEVICE reports a "recovered error". The requested command was successful.
		if(ret == 0 || ret == 21) {
			break;
		}

		// These ones seem like we should retry with the delay... AKA no need to check
		// 26     the  DEVICE  reports  a SCSI status of "busy". (can retry)
		// 47     flock (Unix system call) error.

		// most other codes don't seem relevent.
		sleep(1);
	}

	/*
	       24     the  DEVICE reports a SCSI status of "reservation conflict". This means access to the DEVICE with the current command
              has been blocked because another machine (HBA or SCSI "initiator") holds a reservation on this DEVICE. On modern SCSI
              systems this is related to the use of the PERSISTENT RESERVATION family of commands.
	*/

	if(strstr(result, "command not supported")) {
		return 0;
	}

	return 1;
}

static pr_dev_err read_reservation(struct device_priv *device, struct device_reservation *res)
{
	char cmd[CMD_LENGTH + 1] = { 0 };
	char line[CMD_RESULT_LENGTH + 1] = { 0 };
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

	snprintf(cmd, CMD_LENGTH, "sg_persist --in --read-reservation %s", device->device_name);
	pipe = popen(cmd, "r");
	if(!pipe) {
		pr_log(LOG_ERR, "Error opening pipe to run command '%s'\n", cmd);
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
			pr_log(LOG_ERR, "Unexpected end of stream while running '%s'\n", cmd);
			return PR_ERR_DEVICE_NOT_OPERABLE;
		}
	}

	pr_log(LOG_ERR, "Unknown error running %s\n", cmd);
	return PR_ERR_UNKNOWN;
}
