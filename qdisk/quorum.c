#include "quorum.h"
#include "algorithm.h"
#include "log.h"


static quorum_handle_t quorum_handle;
static quorum_callbacks_t callbacks = { quorum_callback };

cs_error_t quorum_init(void)
{
	cs_error_t rc = CS_OK;
	uint32_t quorum_type = QUORUM_SET;
	// Create quorum handle
	rc = quorum_initialize(&quorum_handle, &callbacks, &quorum_type);

	if(CS_OK == rc) {
		printf("Quorum initalize was successfull\n");
	} else {
		printf("Quorum initalize was not successfull\n");
		return rc;
	}

	rc = quorum_trackstart(quorum_handle, CS_TRACK_CHANGES);
	return rc;
}

void quorum_dispatch_all(void)
{
	quorum_dispatch(quorum_handle, CS_DISPATCH_ALL);
}
