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

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include <bluetooth/bluetooth.h>

#include "gattlib_internal.h"

#include "att.h"
#include "btio.h"
#include "gattrib.h"
#include "hci.h"
#include "hci_lib.h"

#define CONNECTION_TIMEOUT    2

struct gattlib_thread_t g_gattlib_thread = { 0 };

typedef struct {
	gatt_connection_t* conn;
	gatt_connect_cb_t  connect_cb;
	int                connected;
	int                timeout;
	GError*            error;
	void*              user_data;
} io_connect_arg_t;

static void events_handler(const uint8_t *pdu, uint16_t len, gpointer user_data) {
	gatt_connection_t *conn = user_data;
	uint8_t opdu[ATT_MAX_MTU];
	uint16_t handle, olen = 0;
	uuid_t uuid = {};

#if BLUEZ_VERSION_MAJOR == 4
	handle = att_get_u16(&pdu[1]);
#else
	handle = get_le16(&pdu[1]);
#endif

	int ret = get_uuid_from_handle(conn, handle, &uuid);
	if (ret) {
		return;
	}

	switch (pdu[0]) {
	case ATT_OP_HANDLE_NOTIFY:
		if (gattlib_has_valid_handler(&conn->notification)) {
			gattlib_call_notification_handler(&conn->notification, &uuid, &pdu[3], len - 3);
		}
		break;
	case ATT_OP_HANDLE_IND:
		if (gattlib_has_valid_handler(&conn->indication)) {
			gattlib_call_notification_handler(&conn->notification, &uuid, &pdu[3], len - 3);
		}
		break;
	default:
		g_print("Invalid opcode\n");
		return;
	}

	if (pdu[0] == ATT_OP_HANDLE_NOTIFY)
		return;

	olen = enc_confirmation(opdu, sizeof(opdu));

	if (olen > 0) {
		gattlib_context_t* conn_context = conn->context;
		g_attrib_send(conn_context->attrib, 0,
#if BLUEZ_VERSION_MAJOR == 4
				opdu[0],
#endif
				opdu, olen, NULL, NULL, NULL);
	}
}

static gboolean io_listen_cb(gpointer user_data) {
	gatt_connection_t *conn = user_data;
	gattlib_context_t* conn_context = conn->context;

	g_attrib_register(conn_context->attrib, ATT_OP_HANDLE_NOTIFY,
#if BLUEZ_VERSION_MAJOR == 5
			GATTRIB_ALL_HANDLES,
#endif
			events_handler, conn, NULL);
	g_attrib_register(conn_context->attrib, ATT_OP_HANDLE_IND,
#if BLUEZ_VERSION_MAJOR == 5
			GATTRIB_ALL_HANDLES,
#endif
			events_handler, conn, NULL);

	return FALSE;
}

static void io_connect_cb(GIOChannel *io, GError *err, gpointer user_data) {
	io_connect_arg_t* io_connect_arg = user_data;

	if (err) {
		io_connect_arg->error = err;

		// Call callback if defined
		if (io_connect_arg->connect_cb) {
			io_connect_arg->connect_cb(NULL, io_connect_arg->user_data);
		}
	} else {
		gattlib_context_t* conn_context = io_connect_arg->conn->context;

#if BLUEZ_VERSION_MAJOR == 4
		conn_context->attrib = g_attrib_new(io);
#else
		conn_context->attrib = g_attrib_new(io, BT_ATT_DEFAULT_LE_MTU, false);
#endif

		//
		// Register the listener callback
		//
		GSource *source = g_idle_source_new ();
		assert(source != NULL);

		g_source_set_callback(source, io_listen_cb, io_connect_arg->conn, NULL);

		// Attaches the listener to the main loop context
		guint id = g_source_attach(source, g_gattlib_thread.loop_context);
		g_source_unref (source);
		assert(id != 0);

		//
		// Save list of characteristics to do the correspondence handle/UUID
		//
		gattlib_discover_char(io_connect_arg->conn, &conn_context->characteristics, &conn_context->characteristic_count);

		//
		// Call callback if defined
		//
		if (io_connect_arg->connect_cb) {
			io_connect_arg->connect_cb(io_connect_arg->conn, io_connect_arg->user_data);
		}

		io_connect_arg->connected = TRUE;
	}
	if (io_connect_arg->connect_cb) {
		free(io_connect_arg);
	}
}

static void *connection_thread(void* arg) {
	struct gattlib_thread_t* loop_thread = arg;

	loop_thread->loop_context = g_main_context_new();
	loop_thread->loop = g_main_loop_new(loop_thread->loop_context, TRUE);

	g_main_loop_run(loop_thread->loop);
	g_main_loop_unref(loop_thread->loop);
	assert(0);
	return NULL;
}

static gatt_connection_t *initialize_gattlib_connection(const gchar *src, const gchar *dst,
		uint8_t dest_type, BtIOSecLevel sec_level, int psm, int mtu,
		gatt_connect_cb_t connect_cb,
		io_connect_arg_t* io_connect_arg)
{
	bdaddr_t sba, dba;
	GError *err = NULL;
	int ret;

	io_connect_arg->error = NULL;

	/* Check if the GattLib thread has been started */
	if (g_gattlib_thread.ref == 0) {
		/* Start it */

		/* Create a thread that will handle Bluetooth events */
		int error = pthread_create(&g_gattlib_thread.thread, NULL, &connection_thread, &g_gattlib_thread);
		if (error != 0) {
			fprintf(stderr, "Cannot create connection thread: %s", strerror(error));
			return NULL;
		}

		/* Wait for the loop to be started */
		while (!g_gattlib_thread.loop || !g_main_loop_is_running (g_gattlib_thread.loop)) {
			usleep(1000);
		}
	} else {
		/* Increase the reference to know how many GATT connection use the loop */
		g_gattlib_thread.ref++;
	}

	/* Remote device */
	if (dst == NULL) {
		fprintf(stderr, "Remote Bluetooth address required\n");
		return NULL;
	}

	ret = str2ba(dst, &dba);
	if (ret != 0) {
		fprintf(stderr, "Destination address '%s' is not valid.\n", dst);
		return NULL;
	}

	/* Local adapter */
	if (src != NULL) {
		if (!strncmp(src, "hci", 3)) {
			hci_devba(atoi(src + 3), &sba);
		} else {
			ret = str2ba(src, &sba);
			if (ret != 0) {
				fprintf(stderr, "Source address '%s' is not valid.\n", src);
				return NULL;
			}
		}
	} else {
		bacpy(&sba, BDADDR_ANY);
	}

	/* Not used for BR/EDR */
	if ((dest_type != BDADDR_LE_PUBLIC) && (dest_type != BDADDR_LE_RANDOM)) {
		return NULL;
	}

	if ((sec_level != BT_IO_SEC_LOW) && (sec_level != BT_IO_SEC_MEDIUM) && (sec_level != BT_IO_SEC_HIGH)) {
		return NULL;
	}

	gattlib_context_t* conn_context = calloc(sizeof(gattlib_context_t), 1);
	if (conn_context == NULL) {
		return NULL;
	}

	gatt_connection_t* conn = calloc(sizeof(gatt_connection_t), 1);
	if (conn == NULL) {
		free(conn_context);
		return NULL;
	}

	conn->context = conn_context;

	/* Intialize bt_io_connect argument */
	io_connect_arg->conn       = conn;
	io_connect_arg->connect_cb = connect_cb;
	io_connect_arg->connected  = FALSE;
	io_connect_arg->timeout    = FALSE;
	io_connect_arg->error      = NULL;

	if (psm == 0) {
		conn_context->io = bt_io_connect(
#if BLUEZ_VERSION_MAJOR == 4
				BT_IO_L2CAP,
#endif
				io_connect_cb, io_connect_arg, NULL, &err,
				BT_IO_OPT_SOURCE_BDADDR, &sba,
#if BLUEZ_VERSION_MAJOR == 5
				BT_IO_OPT_SOURCE_TYPE, BDADDR_LE_PUBLIC,
#endif
				BT_IO_OPT_DEST_BDADDR, &dba,
				BT_IO_OPT_DEST_TYPE, dest_type,
				BT_IO_OPT_CID, ATT_CID,
				BT_IO_OPT_SEC_LEVEL, sec_level,
				BT_IO_OPT_TIMEOUT, CONNECTION_TIMEOUT,
				BT_IO_OPT_INVALID);
	} else {
		conn_context->io = bt_io_connect(
#if BLUEZ_VERSION_MAJOR == 4
				BT_IO_L2CAP,
#endif
				io_connect_cb, io_connect_arg, NULL, &err,
				BT_IO_OPT_SOURCE_BDADDR, &sba,
#if BLUEZ_VERSION_MAJOR == 5
				BT_IO_OPT_SOURCE_TYPE, BDADDR_LE_PUBLIC,
#endif
				BT_IO_OPT_DEST_BDADDR, &dba,
				BT_IO_OPT_PSM, psm,
				BT_IO_OPT_IMTU, mtu,
				BT_IO_OPT_SEC_LEVEL, sec_level,
				BT_IO_OPT_TIMEOUT, CONNECTION_TIMEOUT,
				BT_IO_OPT_INVALID);
	}

	if (err) {
		fprintf(stderr, "%s\n", err->message);
		g_error_free(err);
		free(conn_context);
		free(conn);
		return NULL;
	} else {
		return conn;
	}
}

static void get_connection_options(unsigned long options, BtIOSecLevel *bt_io_sec_level, int *psm, int *mtu) {
	if (options & GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_LOW) {
		*bt_io_sec_level = BT_IO_SEC_LOW;
	} else if (options & GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_MEDIUM) {
		*bt_io_sec_level = BT_IO_SEC_MEDIUM;
	} else if (options & GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_HIGH) {
		*bt_io_sec_level = BT_IO_SEC_HIGH;
	} else {
		*bt_io_sec_level = BT_IO_SEC_SDP;
	}

	*psm = GATTLIB_CONNECTION_OPTIONS_LEGACY_GET_PSM(options);
	*mtu = GATTLIB_CONNECTION_OPTIONS_LEGACY_GET_MTU(options);
}

gatt_connection_t *gattlib_connect_async(void *adapter, const char *dst,
				unsigned long options,
				gatt_connect_cb_t connect_cb, void* data)
{
	const char *adapter_mac_address;
	gatt_connection_t *conn;
	BtIOSecLevel bt_io_sec_level;
	int psm, mtu;

	if (adapter != NULL) {
		fprintf(stderr, "Missing support");
		assert(0); // Need to add support
		return NULL;
	} else {
		adapter_mac_address = NULL;
	}

	// Check parameters
	if ((options & (GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_PUBLIC | GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDOM)) == 0) {
		// Please, set GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_PUBLIC or
		// GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDMON
		fprintf(stderr, "gattlib_connect_async() expects address type.\n");
		return NULL;
	}

	get_connection_options(options, &bt_io_sec_level, &psm, &mtu);

	io_connect_arg_t* io_connect_arg = malloc(sizeof(io_connect_arg_t));
	if (io_connect_arg == NULL) {
		return NULL;
	}
	io_connect_arg->user_data = data;

	if (options & GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_PUBLIC) {
		conn = initialize_gattlib_connection(adapter_mac_address, dst, BDADDR_LE_PUBLIC, bt_io_sec_level,
						     psm, mtu, connect_cb, io_connect_arg);
		if (conn != NULL) {
			return conn;
		}
	}

	if (options & GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDOM) {
		conn = initialize_gattlib_connection(adapter_mac_address, dst, BDADDR_LE_RANDOM, bt_io_sec_level,
						     psm, mtu, connect_cb, io_connect_arg);
	}

	return conn;
}

static gboolean connection_timeout(gpointer user_data) {
	io_connect_arg_t* io_connect_arg = user_data;

	io_connect_arg->timeout = TRUE;

	return FALSE;
}

/**
 * @brief Function to connect to a BLE device
 *
 * @param src          Local Adaptater interface
 * @param dst          Remote Bluetooth address
 * @param dst_type     Set LE address type (either BDADDR_LE_PUBLIC or BDADDR_LE_RANDOM)
 * @param sec_level    Set security level (either BT_IO_SEC_LOW, BT_IO_SEC_MEDIUM, BT_IO_SEC_HIGH)
 * @param psm          Specify the PSM for GATT/ATT over BR/EDR
 * @param mtu          Specify the MTU size
 */
static gatt_connection_t *gattlib_connect_with_options(const char *src, const char *dst,
						       uint8_t dest_type, BtIOSecLevel bt_io_sec_level, int psm, int mtu)
{
	GSource* timeout;
	gatt_connection_t *conn;
	io_connect_arg_t io_connect_arg;

	conn = initialize_gattlib_connection(src, dst, dest_type, bt_io_sec_level,
			psm, mtu, NULL, &io_connect_arg);
	if (conn == NULL) {
		if (io_connect_arg.error) {
			fprintf(stderr, "Error: gattlib_connect - initialization error:%s\n", io_connect_arg.error->message);
		} else {
			fprintf(stderr, "Error: gattlib_connect - initialization\n");
		}
		return NULL;
	}

	// Timeout of 'CONNECTION_TIMEOUT+4' seconds
	timeout = gattlib_timeout_add_seconds(CONNECTION_TIMEOUT + 4, connection_timeout, &io_connect_arg);

	// Wait for the connection to be done
	while ((io_connect_arg.connected == FALSE) && (io_connect_arg.timeout == FALSE)) {
		g_main_context_iteration(g_gattlib_thread.loop_context, FALSE);
	}
	
	// Disconnect the timeout source if connection success
	if (io_connect_arg.connected) g_source_destroy(timeout);

	if (io_connect_arg.timeout) {
		return NULL;
	}

	if (io_connect_arg.error) {
		fprintf(stderr, "gattlib_connect - connection error:%s\n", io_connect_arg.error->message);
		return NULL;
	} else {
		return conn;
	}
}


/**
 * @brief Function to connect to a BLE device
 *
 * @param src		Local Adaptater interface
 * @param dst		Remote Bluetooth address
 * @param options	Options to connect to BLE device. See `GATTLIB_CONNECTION_OPTIONS_*`
 */
gatt_connection_t *gattlib_connect(void* adapter, const char *dst, unsigned long options)
{
	const char* adapter_mac_address;
	gatt_connection_t *conn;
	BtIOSecLevel bt_io_sec_level;
	int psm, mtu;

	if (adapter != NULL) {
		fprintf(stderr, "Missing support");
		assert(0); // Need to add support
		return NULL;
	} else {
		adapter_mac_address = NULL;
	}

	// Check parameters
	if ((options & (GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_PUBLIC | GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDOM)) == 0) {
		// Please, set GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_PUBLIC or
		// GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDMON
		fprintf(stderr, "gattlib_connect() expects address type.\n");
		return NULL;
	}

	get_connection_options(options, &bt_io_sec_level, &psm, &mtu);

	if (options & GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_PUBLIC) {
		conn = gattlib_connect_with_options(adapter_mac_address, dst, BDADDR_LE_PUBLIC, bt_io_sec_level, psm, mtu);
		if (conn != NULL) {
			return conn;
		}
	}

	if (options & GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDOM) {
		conn = gattlib_connect_with_options(adapter_mac_address, dst, BDADDR_LE_RANDOM, bt_io_sec_level, psm, mtu);
	}

	return conn;
}

int gattlib_disconnect(gatt_connection_t* connection) {
	gattlib_context_t* conn_context = connection->context;

#if BLUEZ_VERSION_MAJOR == 4
	// Stop the I/O Channel
	GIOStatus status = g_io_channel_shutdown(conn_context->io, FALSE, NULL);
	assert(status == G_IO_STATUS_NORMAL);
	g_io_channel_unref(conn_context->io);
#endif

	g_attrib_unref(conn_context->attrib);

	free(conn_context->characteristics);
	free(connection->context);
	free(connection);

	//TODO: Add a mutex around this code to avoid a race condition
	/* Decrease the reference counter of the loop */
	g_gattlib_thread.ref--;
	/* Check if we are the last one */
	if (g_gattlib_thread.ref == 0) {
		g_main_loop_quit(g_gattlib_thread.loop);
		g_main_loop_unref(g_gattlib_thread.loop);
		g_main_context_unref(g_gattlib_thread.loop_context);

		// Detach the thread
		pthread_detach(g_gattlib_thread.thread);
	}

	return GATTLIB_SUCCESS;
}

GSource* gattlib_watch_connection_full(GIOChannel* io, GIOCondition condition,
								 GIOFunc func, gpointer user_data, GDestroyNotify notify)
{
	// Create a main loop source
	GSource *source = g_io_create_watch (io, condition);
	assert(source != NULL);

	g_source_set_callback (source, (GSourceFunc)func, user_data, notify);

	// Attaches it to the main loop context
	guint id = g_source_attach(source, g_gattlib_thread.loop_context);
	g_source_unref (source);
	assert(id != 0);

	return source;
}

GSource* gattlib_timeout_add_seconds(guint interval, GSourceFunc function, gpointer data) {
	GSource *source = g_timeout_source_new_seconds(interval);
	assert(source != NULL);

	g_source_set_callback(source, function, data, NULL);

	// Attaches it to the main loop context
	guint id = g_source_attach(source, g_gattlib_thread.loop_context);
	g_source_unref (source);
	assert(id != 0);

	return source;
}

int get_uuid_from_handle(gatt_connection_t* connection, uint16_t handle, uuid_t* uuid) {
	gattlib_context_t* conn_context = connection->context;
	int i;

	for (i = 0; i < conn_context->characteristic_count; i++) {
		if (conn_context->characteristics[i].value_handle == handle) {
			memcpy(uuid, &conn_context->characteristics[i].uuid, sizeof(uuid_t));
			return GATTLIB_SUCCESS;
		}
	}
	return GATTLIB_NOT_FOUND;
}

int get_handle_from_uuid(gatt_connection_t* connection, const uuid_t* uuid, uint16_t* handle) {
	gattlib_context_t* conn_context = connection->context;
	int i;

	for (i = 0; i < conn_context->characteristic_count; i++) {
		if (gattlib_uuid_cmp(&conn_context->characteristics[i].uuid, uuid) == 0) {
			*handle = conn_context->characteristics[i].value_handle;
			return GATTLIB_SUCCESS;
		}
	}
	return GATTLIB_NOT_FOUND;
}

#if 0 // Disable until https://github.com/labapart/gattlib/issues/75 is resolved
int gattlib_get_rssi(gatt_connection_t *connection, int16_t *rssi)
{
	return GATTLIB_NOT_SUPPORTED;
}
#endif

int gattlib_get_rssi_from_mac(void *adapter, const char *mac_address, int16_t *rssi)
{
	return GATTLIB_NOT_SUPPORTED;
}
