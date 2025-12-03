#include "cmap.h"
#include "log.h"

static cmap_handle_t cmap_handle;

cs_error_t qdisk_cmap_init()
{
	if(cmap_initialize(&cmap_handle) != CS_OK) {
		fprintf(stderr, "Cannot initialize CMAP service\n");
		cmap_handle = 0;
		return CS_ERR_INIT;
	}
	return CS_OK;
}

cs_error_t qdisk_cmap_get_node_count(uint32_t *node_count)
{
	if(0 == cmap_handle) {
		return CS_ERR_INIT;
	}
	return cmap_get_uint32(cmap_handle, "nodelist.node_count", node_count);
}

cs_error_t qdisk_cmap_get_qdisk_device(char **device_path)
{
	if(0 == cmap_handle) {
		return CS_ERR_INIT;
	}
	return cmap_get_string(cmap_handle, "quorum.device.disk.device", device_path);
}

cs_error_t qdisk_cmap_get_qdisk_device_wwn(char **wwn)
{
	if(0 == cmap_handle) {
		return CS_ERR_INIT;
	}
	return cmap_get_string(cmap_handle, "quorum.device.disk.wwn", wwn);
}

cs_error_t qdisk_cmap_get_qdisk_key_file(char **key_file)
{
	if(0 == cmap_handle) {
		return CS_ERR_INIT;
	}
	return cmap_get_string(cmap_handle, "quorum.device.disk.keyfile", key_file);
}

cs_error_t qdisk_cmap_get_heartbeat(uint32_t *heartbeat)
{
	if(0 == cmap_handle) {
		return CS_ERR_INIT;
	}
	return cmap_get_uint32(cmap_handle, "quorum.device.disk.heartbeat", heartbeat);
}

cs_error_t qdisk_cmap_get_timeout(uint32_t *timeout)
{
	if(0 == cmap_handle) {
		return CS_ERR_INIT;
	}
	return cmap_get_uint32(cmap_handle, "quorum.device.disk.timeout", timeout);
}
cs_error_t qdisk_cmap_get_qdisk_can_operate(uint8_t *qdisk_can_operate)
{
	if(0 == cmap_handle) {
		return CS_ERR_INIT;
	}
	return cmap_get_uint8(cmap_handle, "runtime.votequorum.qdisk_can_operate", qdisk_can_operate);
}
