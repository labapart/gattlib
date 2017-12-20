/*
 *
 *  GattLib Async - GATT Library Asynchronous functions
 *
 *  Copyright (C) 2017 Pat Deegan, psychogenic.com
 *
 *   Async expansions to the GattLib library.
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



/*
 * *** Async Notes ***
 *
 *  Async versions of the worst-offenders (slowest/longest blocking)
 *  are now available and implemented here.
 *
 *  These operations hey need some time allocated
 *  in order to process events, trigger callbacks, etc.  The simplest
 *  way to do this is to periodically give them a moment in your main
 *  loop, eg
 *
 *  while (1) {
 *  	// handle user events
 *  	// do processing, etc.
 *
 *  	// process any async events
 *  	gattlib_async_process();
 *
 *  }
 *
 *
 *
 *
 */

#include <glib.h>

#include <stdbool.h>
#include <stdlib.h>

#include "gattlib_internal.h"

/* decl -- implemented in gattlib.c, re-used here */
int gattlib_adapter_scan_enable_setup(void* adapter, gattlib_discovered_device_t discovered_device_cb,
	GDBusObjectManager **dev_manager);


#define GATTLIB_ASYNC_LOOP_CREATECONTEXT		TRUE
/*
 * ************* Async Structures and Utility Functions *************
 */
typedef void (*async_cleanuptime_cb_t)(void);

/* our state... single struct to hold anything our async
 * stuff needs.
 */
typedef struct gattlib_async_statestruct {

	/*
	 * async functions will have a specific "main" loop
	 * associated with them, so they can run tasks/handle
	 * timeouts etc.
	 */
	GMainContext *current_context;
	GMainLoop * current_loop;
	GSource *	timeout_source;


	/*
	 * callbacks they may be assigned for specific _async()
	 * calls.
	 * done_callback is a generic "this method is done" call.
	 *
	 */
	gattlib_async_completed_cb_t done_callback;
	gattlib_async_error_cb_t error_callback;
	gatt_connect_cb_t connection_done_cb;
	gboolean am_scanning;

	/*
	 * internals
	 */
	async_cleanuptime_cb_t cleanup;
	void * async_proc_data;

} gattlib_async_state_t;

static volatile gattlib_async_state_t gattlib_async_global_state = { 0 };

/*
 * gattlib_async_quit_currentloop() -- mark the current loop as done
 */
static void gattlib_async_quit_currentloop() {
	DEBUG_GATTLIB("async_quit_currentloop()\n");
	if (gattlib_async_global_state.current_loop) {
		g_main_loop_quit(gattlib_async_global_state.current_loop);
	} else {
		DEBUG_GATTLIB("booo, no loop?\n");
	}
}



/*
 * gattlib_timeout_async_loop() -- general callback for async loop
 * timeouts.
 */
static gboolean gattlib_timeout_async_loop(gpointer data) {
	DEBUG_GATTLIB("\nasync timeout called\n");
	if (data) {
		// if (gattlib_async_global_state.current_loop) {
		DEBUG_GATTLIB("sending timeout quit to loop\n");
		// g_main_loop_quit(gattlib_async_global_state.current_loop);
		g_main_loop_quit(data);
	}

	return FALSE;
}

/*
 * gattlib_async_setup_currentloop(TIMEOUT, CREATE_CONTEXT)
 * Utility function to setup a "main loop" for async operations.
 * Will fail if a loop is already alive and current.
 *
 * return 0 on success
 */
int gattlib_async_setup_currentloop(int timeout, gboolean useOwnContext) {
	DEBUG_GATTLIB("setting up async loop...");
	if (gattlib_async_global_state.current_loop) {
		DEBUG_GATTLIB("boo, already done\n");
		return 1;
	}
	if (useOwnContext == TRUE) {

		DEBUG_GATTLIB("with own context.\n");
		gattlib_async_global_state.current_context = g_main_context_new();
		gattlib_async_global_state.current_loop = g_main_loop_new(
				gattlib_async_global_state.current_context, TRUE);
	} else {
		DEBUG_GATTLIB("with global context.\n");
		gattlib_async_global_state.current_loop = g_main_loop_new(NULL, TRUE);
	}


	if (! gattlib_async_global_state.current_loop) {
		ERROR_GATTLIB("async loop setup: could not get new loop??\n");
		return 1;
	}
	if (timeout) {
		DEBUG_GATTLIB("Adding a timeout to loop of %i seconds\n", timeout);

		gattlib_async_global_state.timeout_source = g_timeout_source_new_seconds(timeout);
		g_source_set_callback (gattlib_async_global_state.timeout_source,
								gattlib_timeout_async_loop,
								gattlib_async_global_state.current_loop,
								NULL);

		g_source_attach(gattlib_async_global_state.timeout_source,
						g_main_loop_get_context(gattlib_async_global_state.current_loop));

	}

	DEBUG_GATTLIB("setup curloop done\n");
	return 0;
}

/*
 * gattlib_async_triggerandclear_donecallback()
 * Utility function to trigger any user-specified "async done" callback,
 * and clear it from the state.
 */
static void gattlib_async_triggerandclear_donecallback() {

	gattlib_async_completed_cb_t dcb = gattlib_async_global_state.done_callback;
	gattlib_async_global_state.done_callback = NULL;

	if (dcb) {
		(dcb)();
	} else {
		DEBUG_GATTLIB("no done cb to trigger\n");
	}
}

/*
 * gattlib_async_teardown_currentloop()
 * Converse of gattlib_async_setup_currentloop() -- destroys and
 * clears out the current main loop from async state.
 *
 * Side effect: will trigger done callback if it's set.
 */
static void gattlib_async_teardown_currentloop() {

	DEBUG_GATTLIB("teardown async loop...");
	if (!gattlib_async_global_state.current_loop) {
		DEBUG_GATTLIB("boo, no loop\n");
		return;
	}

	g_main_loop_quit(gattlib_async_global_state.current_loop); // TEST

	if (gattlib_async_global_state.timeout_source) {
		g_source_unref(gattlib_async_global_state.timeout_source);
		gattlib_async_global_state.timeout_source = NULL;
	}

	g_main_loop_unref(gattlib_async_global_state.current_loop);
	gattlib_async_global_state.current_loop = NULL;

	if (gattlib_async_global_state.current_context) {
		g_main_context_unref(gattlib_async_global_state.current_context);
		gattlib_async_global_state.current_context = NULL;
	}

	if (gattlib_async_global_state.cleanup) {
		(gattlib_async_global_state.cleanup)();
		gattlib_async_global_state.cleanup = NULL;
	}


	DEBUG_GATTLIB("teardown trigger done cb()\n");

	gattlib_async_triggerandclear_donecallback();

	DEBUG_GATTLIB("teardown done\n");

}

/*
 * gattlib_async_process()
 *
 * Allots a time slice to process events for currently setup
 * async main loop.
 */
int gattlib_async_process() {

	// always tick the main context, so bluez signals can get through
	g_main_context_iteration(NULL, FALSE);

	if (!gattlib_async_global_state.current_loop) {

		return 1;
	}

	// DEBUG_GATTLIB("!");
	if (!g_main_loop_is_running(gattlib_async_global_state.current_loop)) {
		DEBUG_GATTLIB("loop run done\n");
		gattlib_async_teardown_currentloop();
		return 1;
	}

	g_main_context_iteration(
			g_main_loop_get_context(gattlib_async_global_state.current_loop),
			FALSE);

	return 0;

}

/*
 * gattlib_async_process_all()
 * Similar to gattlib_async_process() but will process events until
 * no more are pending in the current loop.
 */
int gattlib_async_process_all() {

	// always tick the main context, so bluez signals can get through
	g_main_context_iteration(NULL, FALSE);
	if (!gattlib_async_global_state.current_loop) {
		return 1 ;
	}

	gboolean moreToProcess = TRUE;

	while ( gattlib_async_global_state.current_loop && (moreToProcess == TRUE)) {
		gattlib_async_process();
		if (gattlib_async_global_state.current_loop) {
			moreToProcess = g_main_context_pending(g_main_loop_get_context(gattlib_async_global_state.current_loop));
		}
	}

	return 0;
}





static void gattlib_async_scan_cleanup(void) {
	DEBUG_GATTLIB("\nasync scan cleanup... ");
	GDBusObjectManager *device_manager = gattlib_async_global_state.async_proc_data;
	if (device_manager) {

		DEBUG_GATTLIB("freeing device manager\n");
		g_object_unref(device_manager);
		gattlib_async_global_state.async_proc_data = NULL;
	} else {
		DEBUG_GATTLIB("no device manager to free\n");
	}
	DEBUG_GATTLIB("am_scanning = FALSE\n");
	gattlib_async_global_state.am_scanning = FALSE;
}

int gattlib_adapter_scan_enable_async(void* adapter, gattlib_discovered_device_t discovered_device_cb, int timeout, gattlib_async_completed_cb_t done_cb) {

	GDBusObjectManager *device_manager;

	DEBUG_GATTLIB("\nscan_enable_async called...");
	if (gattlib_async_setup_currentloop(timeout, GATTLIB_ASYNC_LOOP_CREATECONTEXT)) {
		// gattlib_async_scan_cleanup();

		DEBUG_GATTLIB("but there's a loop running already.\n");
		return 1;
	}


	DEBUG_GATTLIB("(am_scanning = TRUE)\n");
	int setupReturn = gattlib_adapter_scan_enable_setup(adapter, discovered_device_cb, &device_manager);
	if (setupReturn)
	{
		DEBUG_GATTLIB("but scan setup failed.\n");
		return setupReturn;
	}


	gattlib_async_global_state.am_scanning = TRUE;
	gattlib_async_global_state.cleanup = gattlib_async_scan_cleanup;
	gattlib_async_global_state.done_callback = done_cb;

	gattlib_async_global_state.async_proc_data = device_manager;

	DEBUG_GATTLIB("and we're go!\n");

	return 0;


}



static
void gattlib_async_scandisable_ready_cb(GObject *source_object,
                        GAsyncResult *res,
                        gpointer user_data) {

	DEBUG_GATTLIB("\nhah! gattlib_async_scandisable_ready_cb called\n");
	gattlib_async_quit_currentloop();
	// gattlib_async_triggerandclear_donecallback();
}


int gattlib_adapter_scan_disable_async(void* adapter, gattlib_async_completed_cb_t done_cb) {

	// likely we're scanning now
	DEBUG_GATTLIB("\nscan disable async called...");

	if (gattlib_async_global_state.current_loop && gattlib_async_global_state.am_scanning) {
		DEBUG_GATTLIB("while scanning... quitting current loop\n");
		gattlib_async_global_state.done_callback = NULL; // cancel that
		gattlib_async_quit_currentloop();
		gattlib_async_process(); // do one run to clean it out.

	}


	DEBUG_GATTLIB("scan disable prep done, calling stop discovery.\n");
	gattlib_async_global_state.done_callback = done_cb;
	gattlib_async_setup_currentloop(20, GATTLIB_ASYNC_LOOP_CREATECONTEXT);
	org_bluez_adapter1_call_stop_discovery((OrgBluezAdapter1*)adapter, NULL,
			gattlib_async_scandisable_ready_cb, NULL);
	return 0;
}






/* gattlib_connect_params_t
 * used to pack all our async connect() call params
 * into a single spot, for easier management
 */
typedef struct {
	char *src;
	char *dst;
	uint8_t dest_type;
	gattlib_bt_sec_level_t sec_level;
	int psm;
	int mtu;
} gattlib_connect_params_t;

/*
 * Async connection functions... this may be more complex than required,
 * but the connect_async() stuff was created using a glib task, so it involves
 * a whole bunch-o-functions working together... bitofamess.
 */
static void gattlib_async_connect_trigger_conn_cb(gatt_connection_t * connptr) {

	if (gattlib_async_global_state.connection_done_cb) {
		DEBUG_GATTLIB("gotta conn cb to call!\n");
		gatt_connect_cb_t thecb = gattlib_async_global_state.connection_done_cb;
		gattlib_async_global_state.done_callback = NULL;
		/* teardown the current loop immediately, in case the callback wants to
		 * set another async op up.
		 */
		gattlib_async_teardown_currentloop();
		gattlib_async_global_state.connection_done_cb = NULL;
		thecb(connptr);
	}

}

static void gattlib_async_connect_timeouttrigger_conn_cb() {
	gattlib_async_global_state.done_callback = NULL; // clear it out.
	gattlib_async_connect_trigger_conn_cb(NULL);
}

static void gattlib_connect_thread_data_free(void * dptr) {
	gattlib_connect_params_t *data = dptr;
	if (data->src) {
		free(data->src);
	}
	if (data->dst) {
		free(data->dst);
	}
	g_free(data);
}
static void gattlib_connect_destroy_unclaimed_connection(gpointer data) {

	DEBUG_GATTLIB("gattlib_connect_destroy_unclaimed_connection\n");
	gatt_connection_t * conn = data;
	if (conn) {
		gattlib_disconnect(conn);
	}

}
static void gattlib_connect_thread_cb(GTask *task, gpointer source_object,
		gpointer task_data, GCancellable *cancellable) {
	DEBUG_GATTLIB("connect thread_cb launched\n");
	gattlib_connect_params_t *data = task_data;
	gatt_connection_t * retval;

	/* Handle cancellation. */
	if (g_task_return_error_if_cancelled(task)) {
		return;
	}

	/* Run the blocking function. */
	retval = gattlib_connect(data->src, data->dst, data->dest_type,
			data->sec_level, data->psm, data->mtu);

	if (!retval) {
		DEBUG_GATTLIB("async conn returned nuffin' !!!\n");
		// gattlib_async_connect_trigger_conn_cb(NULL);

	}
	g_task_return_pointer(task, retval,
			gattlib_connect_destroy_unclaimed_connection);

}

void gattlib_connect_async_glib(const char *src, const char *dst,
		uint8_t dest_type, gattlib_bt_sec_level_t sec_level, int psm, int mtu,
		/* gatt_connect_cb_t connect_cb, */
		GCancellable *cancellable, GAsyncReadyCallback callback,
		gpointer user_data) {
	GTask *task = NULL; /* owned */
	gattlib_connect_params_t *data = NULL; /* owned */

	DEBUG_GATTLIB("gattlib_connect_async_glib\n");

	/* g_return_if_fail (src && strlen(src)); */
	/* g_return_if_fail (dst && strlen(dst)); */
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	task = g_task_new(NULL, cancellable, callback, user_data);
	g_task_set_source_tag(task, gattlib_connect_async_glib);

	/* Cancellation should be handled manually using mechanisms specific to
	 * some_blocking_function(). */
	g_task_set_return_on_cancel(task, FALSE);

	/* Set up a closure containing the call’s parameters. Copy them to avoid
	 * locking issues between the calling thread and the worker thread. */
	data = g_new0(gattlib_connect_params_t, 1);
	if (src) {
		data->src = malloc(strlen(src) + 1);
		if (data->src) {
			strcpy(data->src, src);
		}
	}

	if (dst) {
		data->dst = malloc(strlen(dst) + 1);
		if (data->dst) {
			strcpy(data->dst, dst);
		}
	}
	data->dest_type = dest_type;
	data->sec_level = sec_level;
	data->psm = psm;
	data->mtu = mtu;
	/* data->connect_cb = connect_cb; */

	g_task_set_task_data(task, data, gattlib_connect_thread_data_free);

	/* Run the task in a worker thread and return immediately while that continues
	 * in the background. When it’s done it will call @callback in the current
	 * thread default main context. */
	g_task_run_in_thread(task, gattlib_connect_thread_cb);

	g_object_unref(task);
}

gatt_connection_t *
gattlib_connect_async_finish(GAsyncResult *result, GError **error) {
	DEBUG_GATTLIB("gattlib_connect_async_finish\n");
	g_return_val_if_fail(g_task_is_valid(result, gattlib_connect_async_glib),
			NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	return g_task_propagate_pointer(G_TASK(result), error);
}

static
void gattlib_async_connect_ready_cb(GObject *source_object, GAsyncResult *res,
		gpointer user_data) {
	GError *error = NULL;
	gatt_connection_t * connptr;
	DEBUG_GATTLIB("gattlib_async_connect_ready_cb\n");
	if (gattlib_async_global_state.connection_done_cb) {
		connptr = g_task_propagate_pointer(G_TASK(res), &error);
		gattlib_async_connect_trigger_conn_cb(connptr);
	}

}

int gattlib_async_connect(const char *src, const char *dst, uint8_t dest_type,
		gattlib_bt_sec_level_t sec_level, int psm, int mtu,
		gatt_connect_cb_t connect_cb) {

	DEBUG_GATTLIB("gattlib_connect_async\n");
	gattlib_async_global_state.connection_done_cb = connect_cb;
	gattlib_async_global_state.done_callback = gattlib_async_connect_timeouttrigger_conn_cb;

	gattlib_async_setup_currentloop(8, GATTLIB_ASYNC_LOOP_CREATECONTEXT);
	gattlib_connect_async_glib(src, dst, dest_type, sec_level, psm, mtu, NULL,
			gattlib_async_connect_ready_cb, NULL);
	return 0;

}


static void gattlib_async_disconnect_ready_cb(GObject *source_object,
		GAsyncResult *res, gpointer user_data) {

	DEBUG_GATTLIB("\nhah! gattlib_async_disconnect_ready_cb called, freeing stuffz\n");
	gatt_connection_t* connection = user_data;
	if (connection) {
		gattlib_context_t* conn_context = connection->context;

		free(conn_context->device_object_path);
		g_object_unref(conn_context->device);

		free(connection->context);
		free(connection);
	}

	gattlib_async_quit_currentloop();
}

int gattlib_disconnect_async(gatt_connection_t* connection,
		gattlib_async_completed_cb_t done_cb) {
	DEBUG_GATTLIB("disconn (async)!\n");

	if (!connection) {
		DEBUG_GATTLIB("boo, no connection!\n");
		return 1;
	}
	gattlib_async_global_state.done_callback = done_cb;
	gattlib_context_t* conn_context = connection->context;

	if (gattlib_async_setup_currentloop(20, GATTLIB_ASYNC_LOOP_CREATECONTEXT)) {
		DEBUG_GATTLIB("boo, can't setup mainloop!\n");
		return 1;
	}

	org_bluez_device1_call_disconnect(conn_context->device, NULL,
			gattlib_async_disconnect_ready_cb, connection);

	return 0;
}




int gattlib_read_char_by_uuid_async(gatt_connection_t* connection, uuid_t* uuid, gatt_read_cb_t gatt_read_cb) {
	OrgBluezGattCharacteristic1 *characteristic = get_characteristic_from_uuid(uuid);
	if (characteristic == NULL) {
		return -1;
	}

	GVariant *out_value;
	GError *error = NULL;

#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 40)
	org_bluez_gatt_characteristic1_call_read_value_sync(
		characteristic, &out_value, NULL, &error);
#else
	GVariantBuilder *options =  g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	org_bluez_gatt_characteristic1_call_read_value_sync(
		characteristic, g_variant_builder_end(options), &out_value, NULL, &error);
	g_variant_builder_unref(options);
#endif
	if (error != NULL) {
		return -1;
	}

	gsize n_elements;
	gconstpointer const_buffer = g_variant_get_fixed_array(out_value, &n_elements, sizeof(guchar));
	if (const_buffer) {
		gatt_read_cb(const_buffer, n_elements);
	}

	g_object_unref(characteristic);

#if BLUEZ_VERSION >= BLUEZ_VERSIONS(5, 40)
	//g_variant_unref(in_params); See: https://github.com/labapart/gattlib/issues/28#issuecomment-311486629
#endif
	return 0;
}


static
void gattlib_async_write_ready_cb(GObject *source_object,
                        GAsyncResult *res,
                        gpointer user_data) {

	DEBUG_GATTLIB("\nhah! gattlib_async_write_ready_cb called ");
	GError *error = NULL;

	if (org_bluez_gatt_characteristic1_call_write_value_finish(
			gattlib_async_global_state.async_proc_data, res, &error) == FALSE) {

		DEBUG_GATTLIB("but had error. %s\n", error->message);
		if (gattlib_async_global_state.error_callback)
		{
			gattlib_async_global_state.done_callback = gattlib_async_global_state.error_callback;
			gattlib_async_global_state.error_callback = NULL;
		}
	} else {

		DEBUG_GATTLIB("and lookin good.\n");
	}

	if ( gattlib_async_global_state.async_proc_data) {
		g_object_unref(gattlib_async_global_state.async_proc_data);
		gattlib_async_global_state.async_proc_data = NULL;
	}


	gattlib_async_quit_currentloop();



}


int gattlib_write_char_by_uuid_async(gatt_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len,
		gattlib_async_completed_cb_t done_cb, gattlib_async_error_cb_t error_cb) {

	DEBUG_GATTLIB("write_by_uuid (async)!\n");

	if (!connection) {
		DEBUG_GATTLIB("boo, no connection!\n");
		if (error_cb) {
			error_cb();
		}
		return 1;
	}

	OrgBluezGattCharacteristic1 *characteristic = get_characteristic_from_uuid(uuid);
	if (characteristic == NULL) {

		DEBUG_GATTLIB("\nwrite() can't find this characteristic!\n");
		if (error_cb) {
			error_cb();
		}
		return -1;
	}

	GVariant *value = g_variant_new_from_data(G_VARIANT_TYPE ("ay"), buffer, buffer_len, TRUE, NULL, NULL);

	gattlib_async_global_state.error_callback = error_cb;
	gattlib_async_global_state.done_callback = done_cb;
	gattlib_async_global_state.async_proc_data = characteristic;
	gattlib_async_setup_currentloop(10, GATTLIB_ASYNC_LOOP_CREATECONTEXT);
#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 40)
	org_bluez_gatt_characteristic1_call_write_value(characteristic, value, NULL, NULL, gattlib_async_write_ready_cb, NULL);
#else
	GVariantBuilder *options =  g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	org_bluez_gatt_characteristic1_call_write_value(characteristic, value, g_variant_builder_end(options), NULL, gattlib_async_write_ready_cb, NULL);
	g_variant_builder_unref(options);
#endif

#if BLUEZ_VERSION >= BLUEZ_VERSIONS(5, 40)
	//g_variant_unref(in_params); See: https://github.com/labapart/gattlib/issues/28#issuecomment-311486629
#endif
	return 0;
}





