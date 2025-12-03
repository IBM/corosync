#include "device.h"
#include "nvme_device.h"
#include "scsi_device.h"

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>

struct persistent_reserve_device *persistent_reserve_device_new(const char *device_path, const char *key_path)
{
	struct persistent_reserve_device *handle = NULL;

	if(is_scsi_device(device_path)) {
		handle = scsi_pr_device_init(device_path, key_path);
	} else if(is_nvme_device(device_path)) {
		handle = nvme_pr_device_init(device_path, key_path);
	} else {
		return NULL;
	}

	return handle;
}

/**
 * Reads a reservation key from the keyfile.
 */
pr_dev_err read_reserve_key(const char *key_file, char *key, size_t key_length)
{
	FILE *file = fopen(key_file, "r");
	if(!file) {
		perror("Failed to open file");
		return PR_ERR_DEVICE_CANT_OPEN_KEYFILE;
	}

	// TODO: don't use fgets
	if(fgets(key, key_length, file) == NULL) {
		perror("Failed to read file");
		fclose(file);
		return PR_ERR_DEVICE_CANT_READ_KEYFILE;
	}
	fclose(file);

	return PR_ERR_OK;
}

static uint32_t jenkins_one_at_a_time_hash(const uint8_t *key, size_t length, uint32_t init)
{
	size_t i = 0;
	uint32_t hash = init;
	while(i != length) {
		hash += key[i++];
		hash += hash << 10;
		hash ^= hash >> 6;
	}
	hash += hash << 3;
	hash ^= hash >> 11;
	hash += hash << 15;
	return hash;
}

/**
 * Generated a new key from a key file and then loads it. If the key file doesn't
 *  exist a new one will be created.  If the key file does exist it will be overwrittern.
 */
pr_dev_err generate_reserve_key(const char *key_file, char *key, size_t key_length)
{
	FILE *file = fopen(key_file, "w");
	if(!file) {
		perror("Failed to open file");
		return PR_ERR_DEVICE_CANT_OPEN_KEYFILE;
	}

	char hostname[_POSIX_HOST_NAME_MAX+1] = { 0 };
	gethostname(hostname, _POSIX_HOST_NAME_MAX);
	for(size_t i = 0; i < key_length / 2; ) {
		uint32_t hash = jenkins_one_at_a_time_hash(hostname, strlen(hostname), i);
		for(size_t j = 0; i < key_length / 2 && j < 32; i++, j+=8) {
			fprintf(file, "%02x", (uint8_t)((hash >> j) % (UINT8_MAX + 1)));
		}
	}

	// int devrandom = open("/dev/urandom", O_RDONLY);
	// if(devrandom < 0) { // Best effort...
	// 	srand((unsigned int)time(NULL));

	// 	for(size_t i = 0; i < key_length / 2; i++) {
	// 		fprintf(file, "%02x", (uint8_t)(rand() % (UINT8_MAX + 1)));
	// 	}
	// } else {
	// 	uint8_t new_key[key_length/2];
	// 	ssize_t curlen = 0;
	// 	while(curlen < key_length/2) {
	// 		ssize_t res = read(devrandom + curlen, new_key, key_length/2 - curlen);
	// 		if(res < 0) continue;
	// 		curlen += res;
	// 	}
	// 	for(size_t i = 0; i < key_length / 2; i++) {
	// 		fprintf(file, "%02x", new_key[i]);
	// 	}
	// 	close(devrandom);
	// }
	fclose(file);
	return read_reserve_key(key_file, key, key_length);
}

pr_dev_err get_reserve_key(const char *key_file, char *key, size_t key_length)
{
	if(access(key_file, F_OK) == 0) {
		return read_reserve_key(key_file, key, key_length);
	}
	printf("Generating key\n");
	return generate_reserve_key(key_file, key, key_length);
}

int exec_cmd(const char *cmd, char *result)
{
	FILE *pipe = popen(cmd, "r");
	if(!pipe) {
		fprintf(stderr, "Error opening pipe to run command");
		return -1;
	}

	// read the command output
	while(fgets(result, CMD_LENGTH, pipe) != NULL) {
	} // FIXME

	// close the pipe
	return pclose(pipe);
}


