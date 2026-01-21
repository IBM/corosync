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
			abort();
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

	// initalize votequorum service
	err = votequorum_initialize(&v_handle, &callbacks);
	if(err != CS_OK) {
		ENGN_LOG(LOG_ERR, "Cannot initialise VOTEQUORUM service");
		v_handle = 0;
		return err;
	}

	ENGN_LOG(LOG_TRACE, "Votequorum initialized. handle:%p,callbacks:%p\n", v_handle, callbacks);
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
	votequorum_getinfo(v_handle, g_our_nodeid, &votequorum_info); // FIXME: handle errors
	return votequorum_info.flags & VOTEQUORUM_INFO_QDISK_RECV_VOTE;
}

cs_error_t vquorum_qdisk_share_ikey(uint64_t key)
{
	cs_error_t err = CS_OK;
	do {
		err = votequorum_qdisk_share_key(v_handle, key);
	} while(CS_ERR_TRY_AGAIN == err);
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
	*key = vq_info.qdisk_ikey;
	return err;
}

