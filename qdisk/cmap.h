#ifndef ENGN_CMAP_H_
#define ENGN_CMAP_H_

#include <unistd.h>
#include <stdint.h>
#include <corosync/cmap.h>

#ifdef __cplusplus
extern "C" {
#endif

cs_error_t qdisk_cmap_init(void);
cs_error_t qdisk_cmap_get_node_count( uint32_t*);
cs_error_t qdisk_cmap_get_qdisk_device(char**);
cs_error_t qdisk_cmap_get_qdisk_device_wwn(char **);
cs_error_t qdisk_cmap_get_qdisk_key_file(char**);
cs_error_t qdisk_cmap_get_heartbeat(uint32_t*);
cs_error_t qdisk_cmap_get_timeout(uint32_t*);
cs_error_t qdisk_cmap_get_qdisk_can_operate(uint8_t*);

#ifdef __cplusplus
}
#endif

#endif
