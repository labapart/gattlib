/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2021 Olivier Martin <olivier@labapart.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __GATTLIB_INTERNAL_H__
#define __GATTLIB_INTERNAL_H__

#include <glib.h>

#define BLUEZ_VERSIONS(major, minor)	(((major) << 8) | (minor))
#define BLUEZ_VERSION					BLUEZ_VERSIONS(BLUEZ_VERSION_MAJOR, BLUEZ_VERSION_MINOR)

#include "gattlib_internal_defs.h"
#include "gattlib.h"

#include "uuid.h"

#if BLUEZ_VERSION_MAJOR == 5
  #include "src/shared/att-types.h"
  #include "src/shared/util.h"
#endif

typedef struct _GAttrib GAttrib;

struct gattlib_thread_t {
	int           ref;
	pthread_t     thread;
	GMainContext* loop_context;
	GMainLoop*    loop;
};

typedef struct {
	GIOChannel*               io;
	GAttrib*                  attrib;

	// We keep a list of characteristics to make the correspondence handle/UUID.
	gattlib_characteristic_t* characteristics;
	int                       characteristic_count;
} gattlib_context_t;

extern struct gattlib_thread_t g_gattlib_thread;

/**
 * Watch the GATT connection for conditions
 */
GSource* gattlib_watch_connection_full(GIOChannel* io, GIOCondition condition,
								 GIOFunc func, gpointer user_data, GDestroyNotify notify);
GSource* gattlib_timeout_add_seconds(guint interval, GSourceFunc function, gpointer data);

void uuid_to_bt_uuid(uuid_t* uuid, bt_uuid_t* bt_uuid);
void bt_uuid_to_uuid(bt_uuid_t* bt_uuid, uuid_t* uuid);

int get_uuid_from_handle(gattlib_connection_t* connection, uint16_t handle, uuid_t* uuid);
int get_handle_from_uuid(gattlib_connection_t* connection, const uuid_t* uuid, uint16_t* handle);

#endif
