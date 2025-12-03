#include "vquorum.h"
#include "log.h"

#include <unistd.h>
#include <inttypes.h>

/* vquorum global vars*/
static votequorum_handle_t v_handle;
static struct votequorum_info info;
static uint32_t g_our_nodeid = 0;
votequorum_ring_id_t g_ring_id = { 0 };

static uint32_t g_node_list_capacity = 0;
static uint32_t g_node_list_entries = 0;
static uint32_t *g_node_list = NULL;

static void votequorum_notification_fn(votequorum_handle_t handle, uint64_t context, votequorum_ring_id_t ring_id,
                                       uint32_t node_list_entries, uint32_t node_list[])
{
	(void)handle;
	(void)context;

	if(g_node_list_capacity < node_list_entries) {
		uint32_t *new_list = realloc(g_node_list, sizeof(*node_list) * node_list_entries);
		if(!new_list) {
			ENGN_LOG(LOG_CRIT, "Can't alloc votequorum node list memory");
			exit(EXIT_FAILURE);
		}
		g_node_list = new_list;
		g_node_list_capacity = node_list_entries;
	}
	memcpy(g_node_list, node_list, sizeof(*node_list) * node_list_entries);
	g_ring_id = ring_id;
}

votequorum_callbacks_t callbacks = {
	.votequorum_quorum_notify_fn = NULL,
	.votequorum_expectedvotes_notify_fn = NULL,
	.votequorum_nodelist_notify_fn = votequorum_notification_fn,
};


cs_error_t vquorum_init(void)
{
	cs_error_t err = CS_OK;

	INIT_LOG();
	// initalize votequorum service
	err = votequorum_initialize(&v_handle, &callbacks);
	if(err != CS_OK) {
		ENGN_LOG(LOG_ERR, "Cannot initialise VOTEQUORUM service");
		v_handle = 0;
		return err;
	}

#ifdef DEBUG
	printf("Votequorum initialized. handle:%p,callbacks:%p\n", v_handle, callbacks);
#endif
	err = votequorum_trackstart(v_handle, 0LL, CS_TRACK_CHANGES);
	if(err != CS_OK) {
		ENGN_LOG(LOG_ERR, "Unable to start votequorum status tracking: %s\n", cs_strerror(err));
		return err;
	}
	return err;
}

unsigned int vquorum_get_total_votes(void)
{
	cs_error_t err = CS_OK;
	err = votequorum_getinfo(v_handle, g_our_nodeid, &info);
	if(CS_OK != err) {
		ENGN_LOG(LOG_NOTICE, "Connection to the votequorum engine interrupted.  Attempting to re-connect.");
		err = vquorum_init();
		if(CS_OK != err) {
			ENGN_LOG(LOG_ERR, "An error occured re-initializing the interface to the votequorum engine ");
		}
	}
	return info.total_votes;
}

void vquorum_get_node_list(uint32_t *num_nodes, uint32_t **node_list)
{
	*num_nodes = g_node_list_entries;
	*node_list = g_node_list;
}

unsigned int vquorum_qdisk_cast_vote(void)
{
	cs_error_t err = CS_OK;

	err = votequorum_dispatch(v_handle, CS_DISPATCH_ALL);
	if(err != CS_OK) {
		ENGN_LOG(LOG_ERR, "Unable to dispatch votequorum status: %s\n", cs_strerror(err));
		return err;
	}

	err = votequorum_qdisk_poll(v_handle, "Qdisk", 1, g_ring_id);
	return info.total_votes;
}

unsigned int vquorum_qdisk_clear_vote(void)
{
	cs_error_t err = CS_OK;

	err = votequorum_dispatch(v_handle, CS_DISPATCH_ALL);
	if(err != CS_OK) {
		ENGN_LOG(LOG_ERR, "Unable to dispatch votequorum status: %s\n", cs_strerror(err));
		return err;
	}

	err = votequorum_qdisk_poll(v_handle, "Qdisk", 0, g_ring_id);
	return info.total_votes;
}

unsigned int vquorum_is_qdisk_recv_vote_flag_set(void)
{
	struct votequorum_info votequorum_info;
	// cs_error_t err = CS_OK;
	// err = 
	votequorum_getinfo(v_handle, g_our_nodeid, &votequorum_info); // FIXME: handle errors
	return votequorum_info.flags & VOTEQUORUM_INFO_QDISK_RECV_VOTE;
}

// cs_error_t vquorum_qdisk_share_key(uint64_t key)
// {
// 	cs_error_t err = votequorum_qdisk_share_key(v_handle, key);
// 	if(CS_OK != err) {
// 		ENGN_LOG(LOG_ERR, "Unable to share persistent reserve key: %s\n", cs_strerror(err));
// 		return err;
// 	}
// 	return CS_OK;
// }

cs_error_t vquorum_qdisk_share_ikey(uint64_t key)
{
	// char skey[16 + 1] = { 0 };
	// cs_error_t err = 0;
	// sprintf(skey, "%016" PRIx64, key);
	// err = votequorum_qdisk_share_key(v_handle, skey);
	// if(CS_OK != err) {
	// 	ENGN_LOG(LOG_ERR, "Unable to share persistent reserve key: %s\n", cs_strerror(err));
	// 	return err;
	// }
	// return CS_OK;
	cs_error_t err = votequorum_qdisk_share_key(v_handle, key);
	if(CS_OK != err) {
		ENGN_LOG(LOG_ERR, "Unable to share persistent reserve key: %s\n", cs_strerror(err));
		return err;
	}
	return CS_OK;
}

cs_error_t vquorum_get_node_key(uint32_t nodeid, uint64_t *key)
{
	cs_error_t err = CS_OK;
	struct votequorum_info vq_info;
	err = votequorum_getinfo(v_handle, nodeid, &vq_info);
	// if(err == CS_OK) {
	// 	sscanf(vq_info.qdisk_key, "%" SCNx64, key);
	// }
	*key = vq_info.qdisk_ikey;
	return err;
}

void vquorum_show(void)
{
	vquorum_get_total_votes();
	// printf("Ring ID:          " CS_PRI_RING_ID "\n", g_ring_id_rep_node, g_ring_id);
	// std::cout << "Expected votes: " << info.node_expected_votes << std::endl;
	// std::cout << "Highest expected: " << info.highest_expected << std::endl;
	// std::cout << "Total votes: " << info.total_votes << std::endl;
	// std::cout << "Quorum: " << info.quorum << std::endl;
	// printf("Quorate: %s\n", g_is_quorate ? "yes":"no");
	printf("Total Votes:%d\n", info.total_votes);
}
