#include "log.h"

static const char* TAGS[] = {
    "UNKNOWN",
    "MAIN",
    "ALGORITHM",
    "VQUORUM",
};

const char *qdisk_log_tag_to_string(uint32_t tags){
    	return TAGS[tags];
}




