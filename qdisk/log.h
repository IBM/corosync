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

const char *qdisk_log_tag_to_string(uint32_t tags);

#define INIT_LOG() do { \
	qb_log_init("tbdisk",LOG_INFO,LOG_INFO); \
	/*map the tags here*/ \
	qb_log_filter_ctl(TAGS_MAIN, QB_LOG_TAG_SET, QB_LOG_FILTER_FILE, "engine/main.c", LOG_TRACE); \
	qb_log_filter_ctl(TAGS_ALGORITHM, QB_LOG_TAG_SET, QB_LOG_FILTER_FILE, "engine/algorithm.c", LOG_TRACE); \
	qb_log_filter_ctl(TAGS_VQUORUM, QB_LOG_TAG_SET, QB_LOG_FILTER_FILE, "engine/vquorum.c", LOG_TRACE); \
	\
	/*setup the callback to print custom tags*/ \
	qb_log_tags_stringify_fn_set(qdisk_log_tag_to_string); \
	\
	/*set the logging format of the syslog messages.  Note that SYSLOG logging is enabled by default.*/   \
	qb_log_format_set(QB_LOG_SYSLOG,"[%g]file:%f,ln:%l  %b"); \
	\
	\
	/*setup and bind LOG_WARNING level messages to stdout with a custom format*/ \
	/*messages will still appear in syslog*/ \
	qb_log_filter_ctl2(QB_LOG_STDOUT, QB_LOG_FILTER_ADD, \
	                   QB_LOG_FILTER_FILE, __FILE__, \
	                   LOG_ALERT, LOG_DEBUG); \
	qb_log_format_set(QB_LOG_STDOUT, "[%g]file:%f,ln:%l  %b"); \
	qb_log_ctl(QB_LOG_STDOUT, QB_LOG_CONF_ENABLED, QB_TRUE); \
	\
} while (0)

#define ENGN_LOG(priority,fmt,args...) do { \
    qb_log(priority,fmt,##args); \
} while (0)
    
#ifdef __cplusplus
}
#endif

#endif
