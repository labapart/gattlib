/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2019 Olivier Martin <olivier@labapart.org>
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

#include <gio/gunixfdlist.h>

#include "gattlib_internal.h"

#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 48)

int gattlib_write_char_by_uuid_stream_open(gatt_connection_t* connection, uuid_t* uuid, gatt_stream_t **stream, uint16_t *mtu)
{
	return GATTLIB_NOT_SUPPORTED;
}

int gattlib_write_char_stream_write(gatt_stream_t *stream, const void *buffer, size_t buffer_len)
{
	return GATTLIB_NOT_SUPPORTED;
}

int gattlib_write_char_stream_close(gatt_stream_t *stream)
{
	return GATTLIB_NOT_SUPPORTED;
}

#else

int gattlib_write_char_by_uuid_stream_open(gatt_connection_t* connection, uuid_t* uuid, gatt_stream_t **stream, uint16_t *mtu)
{
	struct dbus_characteristic dbus_characteristic = get_characteristic_from_uuid(connection, uuid);
	GError *error = NULL;
	GUnixFDList *fd_list;
	GVariant *out_fd;
	int fd;

	GVariantBuilder *variant_options = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	org_bluez_gatt_characteristic1_call_acquire_write_sync(
		dbus_characteristic.gatt,
		g_variant_builder_end(variant_options),
		NULL /* fd_list */,
	    &out_fd, mtu,
		&fd_list,
	    NULL /* cancellable */, &error);

	g_variant_builder_unref(variant_options);

	if (error != NULL) {
		fprintf(stderr, "Failed to acquired write DBus GATT characteristic: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	error = NULL;
	fd = g_unix_fd_list_get(fd_list, g_variant_get_handle(out_fd), &error);
	if (error != NULL) {
		fprintf(stderr, "Failed to retrieve Unix File Descriptor: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	// We abuse the pointer 'stream' to pass the 'File Descriptor'
	*stream = (gatt_stream_t*)(unsigned long)fd;

	return GATTLIB_SUCCESS;
}

int gattlib_write_char_stream_write(gatt_stream_t *stream, const void *buffer, size_t buffer_len)
{
	write((unsigned long)stream, buffer, buffer_len);
	return GATTLIB_SUCCESS;
}

int gattlib_write_char_stream_close(gatt_stream_t *stream)
{
	close((unsigned long)stream);
	return GATTLIB_SUCCESS;
}

#endif /* #if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 48) */
