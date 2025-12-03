#ifndef ENGN_VOTEQUORUM_H_
#define ENGN_VOTEQUORUM_H_

#include <unistd.h>
#include <stdint.h>

#include <corosync/votequorum.h>

#ifdef __cplusplus
extern "C" {
#endif

cs_error_t vquorum_init(void);
unsigned int vquorum_get_total_votes(void);
unsigned int vquorum_qdisk_cast_vote(void);
unsigned int vquorum_qdisk_clear_vote(void);
unsigned int vquorum_is_qdisk_recv_vote_flag_set(void);
cs_error_t vquorum_qdisk_share_key(const char* key);
cs_error_t vquorum_qdisk_share_ikey(uint64_t key);
cs_error_t vquorum_get_node_key(uint32_t nodeid, uint64_t *key);
void vquorum_show(void);

void vquorum_get_node_list(uint32_t *num_nodes, uint32_t **node_list);

#ifdef __cplusplus
}
#endif

#endif
