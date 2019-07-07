#ifndef __GATTLIB_INTERNAL_DEFS_H__
#define __GATTLIB_INTERNAL_DEFS_H__

#include "gattlib.h"

struct _gatt_connection_t {
	void* context;

	gattlib_event_handler_t notification_handler;
	void* notification_user_data;

	gattlib_event_handler_t indication_handler;
	void* indication_user_data;

	gattlib_disconnection_handler_t disconnection_handler;
	void* disconnection_user_data;
};

#endif
