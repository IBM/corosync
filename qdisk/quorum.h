#ifndef ENGN_QUORUM_H_
#define ENGN_QUORUM_H_

#include <unistd.h>
#include <stdint.h>

#include <corosync/quorum.h>

#ifdef __cplusplus
extern "C" {
#endif

cs_error_t quorum_init(void);
void quorum_dispatch_all(void);

#ifdef __cplusplus
}
#endif

#endif
