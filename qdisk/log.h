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

#ifndef ENGN_LOG_H_
#define ENGN_LOG_H_

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <qb/qblog.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAGS_MAIN 1
#define TAGS_ALGORITHM 2
#define TAGS_VQUORUM 3
#define TAGS_PR 4

const char *qdisk_log_tag_to_string(uint32_t tags);

#define INIT_LOG() do { \
	qb_log_init("tbdisk",LOG_INFO,LOG_INFO); \
	/*map the tags here*/ \
	qb_log_filter_ctl(TAGS_MAIN, QB_LOG_TAG_SET, QB_LOG_FILTER_FILE, "main.c", LOG_TRACE); \
	qb_log_filter_ctl(TAGS_ALGORITHM, QB_LOG_TAG_SET, QB_LOG_FILTER_FILE, "algorithm.c", LOG_TRACE); \
	qb_log_filter_ctl(TAGS_VQUORUM, QB_LOG_TAG_SET, QB_LOG_FILTER_FILE, "vquorum.c", LOG_TRACE); \
	\
	/*setup the callback to print custom tags*/ \
	qb_log_tags_stringify_fn_set(qdisk_log_tag_to_string); \
	\
	/*set the logging format of the syslog messages.  Note that SYSLOG logging is enabled by default.*/   \
	qb_log_format_set(QB_LOG_SYSLOG,"[%g]file:%f,ln:%l  %b"); \
	\
	\
	/*setup blackbox handling*/ \
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_TRUE); \
	qb_log_filter_ctl(QB_LOG_BLACKBOX, QB_LOG_FILTER_ADD, QB_LOG_FILTER_FILE, "*", LOG_TRACE); \
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_SIZE, 16*1024); \
	qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_THREADED, QB_FALSE); \
	\
} while (0)

#define ENGN_LOG(priority,fmt,args...) do { \
    qb_log(priority,fmt,##args); \
} while (0)
    
#ifdef __cplusplus
}
#endif

#endif
