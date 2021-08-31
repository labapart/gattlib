/*
 * SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
 *
 * Copyright (c) 2021, Olivier Martin <olivier@labapart.org>
 */

#ifndef __GATTLIB_INTERNAL_DEFS_H__
#define __GATTLIB_INTERNAL_DEFS_H__

#include <stdbool.h>

#include "gattlib.h"

enum handler_type { UNKNOWN = 0, NATIVE_NOTIFICATION, NATIVE_DISCONNECTION, PYTHON };

struct gattlib_handler {
	enum handler_type type;
	union {
		gattlib_event_handler_t notification_handler;
		gattlib_disconnection_handler_t disconnection_handler;
		void* python_handler;
	};
	void* user_data;
};

struct _gatt_connection_t {
	void* context;

	struct gattlib_handler notification;
	struct gattlib_handler indication;
	struct gattlib_handler disconnection;
};

bool gattlib_has_valid_handler(struct gattlib_handler *handler);
void gattlib_call_disconnection_handler(struct gattlib_handler *handler);
void gattlib_call_notification_handler(struct gattlib_handler *handler, const uuid_t* uuid, const uint8_t* data, size_t data_length);

#endif
