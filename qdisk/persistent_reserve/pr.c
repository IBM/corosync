#include "persistent_reserve/device.h"

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

static const char* default_key_file = "/var/lib/misc/reserve_key";

static void print_usage(FILE *f, const char *exe)
{
    fprintf(f, "Usage:\n");
    fprintf(f, "\t%s <register|unregister|reserve|release|abort> <device>", exe);
}

int main(int argc, const char * argv[])
{
    struct persistent_reserve_device* handle = NULL;


    if(argc != 3) {
        fprintf(stderr, "Invalid command syntax\n\n");
        print_usage(stderr, argv[0]);
        return 1;
    }

    if( !(is_scsi_device(argv[2]) || is_nvme_device(argv[2])) ) {
        fprintf(stderr, "The device %s is neither SCSI or NVME.  Aborting command.\n\n", argv[2]);
        print_usage(stderr, argv[0]);
        return 1;
    }

    handle = persistent_reserve_device_new(argv[2], default_key_file);
    if(!handle) {
        fprintf(stderr, "Failed to open device %s. Aborting command.\n\n", argv[2]);
        return 1;
    }

    if(strcmp(argv[1],"register") == 0) {
        persistent_reserve_device_register_key(handle);
    }
    else if(strcmp(argv[1],"unregister") == 0) {
        persistent_reserve_device_unregister_key(handle);
    }
    else if(strcmp(argv[1],"reserve") == 0) {
        persistent_reserve_device_reserve(handle);
    }
    else if(strcmp(argv[1],"release") == 0) {
        persistent_reserve_device_release(handle);
    }
    else if(strcmp(argv[1],"abort") == 0) {
        persistent_reserve_device_abort(handle);
    }
    else {
        fprintf(stderr, "Invalid action: '%s'\n\n", argv[1]);
        print_usage(stderr, argv[0]);
        return 1;
    }
    return 0;
} 
