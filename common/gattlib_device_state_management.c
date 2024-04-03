/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024, Olivier Martin <olivier@labapart.org>
 */

#include "gattlib_internal.h"

const char* device_state_str[] = {
    "NOT_FOUND",
	"CONNECTING",
	"CONNECTED",
	"DISCONNECTING",
	"DISCONNECTED"
};

static gint _compare_device_with_device_id(gconstpointer a, gconstpointer b) {
    const struct _gattlib_device* device = a;
    const char* device_id = b;

    return g_ascii_strcasecmp(device->device_id, device_id);
}

static GSList* _find_device_with_device_id(struct gattlib_adapter* adapter, const char* device_id) {
    return g_slist_find_custom(adapter->devices, device_id, _compare_device_with_device_id);
}

struct _gattlib_device* gattlib_device_get_device(void* adapter, const char* device_id) {
    struct gattlib_adapter* gattlib_adapter = adapter;
    struct _gattlib_device* device = NULL;

    g_rec_mutex_lock(&gattlib_adapter->mutex);

    GSList *item = _find_device_with_device_id(gattlib_adapter, device_id);
    if (item == NULL) {
        goto EXIT;
    }

    device = (struct _gattlib_device*)item->data;

EXIT:
    g_rec_mutex_unlock(&gattlib_adapter->mutex);
    return device;
}

enum _gattlib_device_state gattlib_device_get_state(void* adapter, const char* device_id) {
    struct gattlib_adapter* gattlib_adapter = adapter;
    enum _gattlib_device_state state = NOT_FOUND;

    g_rec_mutex_lock(&gattlib_adapter->mutex);

    struct _gattlib_device* device = gattlib_device_get_device(adapter, device_id);
    if (device != NULL) {
        state = device->state;
    }

    g_rec_mutex_unlock(&gattlib_adapter->mutex);
    return state;
}

int gattlib_device_set_state(void* adapter, const char* device_id, enum _gattlib_device_state new_state) {
    struct gattlib_adapter* gattlib_adapter = adapter;
    enum _gattlib_device_state old_state;
    int ret = GATTLIB_SUCCESS;

    g_rec_mutex_lock(&gattlib_adapter->mutex);

    old_state = gattlib_device_get_state(adapter, device_id);
    if (old_state == NOT_FOUND) {
        //
        // The device does not exist yet
        //
        if (new_state != NOT_FOUND) {
            struct _gattlib_device* device = calloc(sizeof(struct _gattlib_device), 1);
            if (device == NULL) {
                GATTLIB_LOG(GATTLIB_ERROR, "gattlib_device_set_state: Cannot allocate device");
                ret = GATTLIB_OUT_OF_MEMORY;
                goto EXIT;
            }

            GATTLIB_LOG(GATTLIB_DEBUG, "gattlib_device_set_state:%s: Set initial state %s", device_id, device_state_str[new_state]);

            device->adapter = adapter;
            device->device_id = g_strdup(device_id);
            device->state = new_state;

            gattlib_adapter->devices = g_slist_append(gattlib_adapter->devices, device);
        } else {
            GATTLIB_LOG(GATTLIB_DEBUG, "gattlib_device_set_state:%s: No state to set", device_id);
        }
    } else if (new_state == NOT_FOUND) {
        //
        // The device needs to be remove and free
        //
        GSList *item = _find_device_with_device_id(gattlib_adapter, device_id);
        if (item == NULL) {
            GATTLIB_LOG(GATTLIB_ERROR, "gattlib_device_set_state: The device is not present. It is not expected");
            ret = GATTLIB_UNEXPECTED;
            goto EXIT;
        }

        struct _gattlib_device* device = item->data;

        switch (device->state) {
        case DISCONNECTED:
            GATTLIB_LOG(GATTLIB_DEBUG, "gattlib_device_set_state: Free device %p", device);
            gattlib_adapter->devices = g_slist_remove(gattlib_adapter->devices, device);
            free(device);
            break;
        case CONNECTING:
            GATTLIB_LOG(GATTLIB_DEBUG, "gattlib_device_set_state: Connecting device needs to be removed - ignore it");
            ret = GATTLIB_UNEXPECTED;
            break;
        case CONNECTED:
            GATTLIB_LOG(GATTLIB_DEBUG, "gattlib_device_set_state: Connecting device needs to be removed - ignore it");
            ret = GATTLIB_UNEXPECTED;
            break;
        case DISCONNECTING:
            GATTLIB_LOG(GATTLIB_DEBUG, "gattlib_device_set_state: Connecting device needs to be removed - ignore it");
            ret = GATTLIB_UNEXPECTED;
            break;
        case NOT_FOUND:
            GATTLIB_LOG(GATTLIB_DEBUG, "gattlib_device_set_state: Not found device needs to be removed - ignore it");
            ret = GATTLIB_UNEXPECTED;
            break;
        }
    } else {
        GATTLIB_LOG(GATTLIB_DEBUG, "gattlib_device_set_state:%s: Set state %s", device_id, device_state_str[new_state]);

        struct _gattlib_device* device = gattlib_device_get_device(adapter, device_id);
        device->state = new_state;
    }

EXIT:
    g_rec_mutex_unlock(&gattlib_adapter->mutex);
    return ret;
}

static void _gattlib_device_free(gpointer data) {
    struct _gattlib_device* device = data;

    switch (device->state) {
    case DISCONNECTED:
        free(device);
        break;
    default:
        GATTLIB_LOG(GATTLIB_WARNING, "Memory of the BLE device '%s' has not been freed because in state %s",
            device->device_id, device_state_str[device->state]);
    }
}

int gattlib_devices_free(void* adapter) {
    struct gattlib_adapter* gattlib_adapter = adapter;

    g_rec_mutex_lock(&gattlib_adapter->mutex);
    g_slist_free_full(gattlib_adapter->devices, _gattlib_device_free);
    g_rec_mutex_unlock(&gattlib_adapter->mutex);

    return 0;
}

static void _gattlib_device_is_disconnected(gpointer data, gpointer user_data) {
    struct _gattlib_device* device = data;
    bool* devices_are_disconnected_ptr = user_data;

    if (device->state != DISCONNECTED) {
        *devices_are_disconnected_ptr = false;
    }
}

int gattlib_devices_are_disconnected(void* adapter) {
    struct gattlib_adapter* gattlib_adapter = adapter;
    bool devices_are_disconnected = true;

    g_rec_mutex_lock(&gattlib_adapter->mutex);
    g_slist_foreach(gattlib_adapter->devices, _gattlib_device_is_disconnected, &devices_are_disconnected);
    g_rec_mutex_unlock(&gattlib_adapter->mutex);

    return devices_are_disconnected;
}

#ifdef DEBUG

static void _gattlib_device_dump_state(gpointer data, gpointer user_data) {
    struct _gattlib_device* device = data;
    GATTLIB_LOG(GATTLIB_DEBUG, "\t%s: %s", device->device_id, device_state_str[device->state]);
}

void gattlib_devices_dump_state(void* adapter) {
    struct gattlib_adapter* gattlib_adapter = adapter;

    g_rec_mutex_lock(&gattlib_adapter->mutex);
    GATTLIB_LOG(GATTLIB_DEBUG, "Device list:");
    g_slist_foreach(gattlib_adapter->devices, _gattlib_device_dump_state, NULL);
    g_rec_mutex_unlock(&gattlib_adapter->mutex);
}

#endif
