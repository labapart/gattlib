/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
 */

#include <errno.h>

#include <gio/gunixfdlist.h>

#include "gattlib_internal.h"

#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 48)

int gattlib_write_char_by_uuid_stream_open(gattlib_connection_t* connection, uuid_t* uuid, gattlib_stream_t **stream, uint16_t *mtu)
{
	return GATTLIB_NOT_SUPPORTED;
}

int gattlib_write_char_stream_write(gattlib_stream_t *stream, const void *buffer, size_t buffer_len)
{
	return GATTLIB_NOT_SUPPORTED;
}

int gattlib_write_char_stream_close(gattlib_stream_t *stream)
{
	return GATTLIB_NOT_SUPPORTED;
}

#else

int gattlib_write_char_by_uuid_stream_open(gattlib_connection_t* connection, uuid_t* uuid, gattlib_stream_t **stream, uint16_t *mtu)
{
	GError *error = NULL;
	GUnixFDList *fd_list;
	GVariant *out_fd;
	int ret;
	int fd;

	//
	// No need of locking the gattlib mutex. get_characteristic_from_uuid() is taking care of the gattlib
	// object coherency. And 'dbus_characteristic' is not linked to gattlib object
	//

	struct dbus_characteristic dbus_characteristic = get_characteristic_from_uuid(connection, uuid);

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
		ret = GATTLIB_ERROR_DBUS_WITH_ERROR(error);
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to acquired write DBus GATT characteristic: %s", error->message);
		g_error_free(error);
		return ret;
	}

	error = NULL;
	fd = g_unix_fd_list_get(fd_list, g_variant_get_handle(out_fd), &error);
	if (error != NULL) {
		ret = GATTLIB_ERROR_DBUS_WITH_ERROR(error);
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to retrieve Unix File Descriptor: %s", error->message);
		g_error_free(error);
		return ret;
	}

	// We abuse the pointer 'stream' to pass the 'File Descriptor'
	*stream = (gattlib_stream_t*)(unsigned long)fd;

	return GATTLIB_SUCCESS;
}

int gattlib_write_char_stream_write(gattlib_stream_t *stream, const void *buffer, size_t buffer_len)
{
	ssize_t ret = write((unsigned long)stream, buffer, buffer_len);
	if (ret < 0) {
		return GATTLIB_ERROR_UNIX_WITH_ERROR(errno);
	} else {
		return GATTLIB_SUCCESS;
	}
}

int gattlib_write_char_stream_close(gattlib_stream_t *stream)
{
	close((unsigned long)stream);
	return GATTLIB_SUCCESS;
}

#endif /* #if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 48) */
