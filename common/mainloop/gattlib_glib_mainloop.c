/*
 * SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
 *
 * Copyright (c) 2021-2024, Olivier Martin <olivier@labapart.org>
 */

/**
 * See some Glib mainloop API function usage in this example:
 * https://github.com/ImageMagick/glib/blob/main/tests/mainloop-test.c
 */

#include "gattlib_internal.h"

// We make this variable global to be able to exit the main loop
static GMainLoop *m_main_loop;

struct _execute_task_arg {
    void* (*task)(void* arg);
    void* arg;
};

static void* _execute_task(void* arg) {
    struct _execute_task_arg *execute_task_arg = arg;
    execute_task_arg->task(execute_task_arg->arg);
    g_main_loop_quit(m_main_loop);
    return NULL;
}

int gattlib_mainloop(void* (*task)(void* arg), void *arg) {
    struct _execute_task_arg execute_task_arg = {
        .task = task,
        .arg = arg
    };
    GError* error;

    if (m_main_loop != NULL) {
        GATTLIB_LOG(GATTLIB_ERROR, "Main loop is already running");
        return GATTLIB_BUSY;
    }

    m_main_loop = g_main_loop_new(NULL, FALSE);

    GThread *task_thread = g_thread_try_new("gattlib_task", _execute_task, &execute_task_arg, &error);
    if (task_thread == NULL) {
        GATTLIB_LOG(GATTLIB_ERROR, "Could not create task for main loop: %s", error->message);
        g_error_free(error);
        return GATTLIB_UNEXPECTED;
    }

    g_main_loop_run(m_main_loop);
    g_main_loop_unref(m_main_loop);

    g_thread_join(task_thread);
    g_thread_unref(task_thread);

    m_main_loop = NULL;
    return GATTLIB_SUCCESS;
}

#if defined(WITH_PYTHON)
struct gattlib_mainloop_handler_python_args {
    PyObject *handler;
    PyObject *user_data;
};

static void* _gattlib_mainloop_handler_python(void* args) {
    struct gattlib_mainloop_handler_python_args* python_args = (struct gattlib_mainloop_handler_python_args*)args;
    PyGILState_STATE d_gstate;
    PyObject *result;

    d_gstate = PyGILState_Ensure();

#if PYTHON_VERSION >= PYTHON_VERSIONS(3, 9)
    result = PyObject_Call(python_args->handler, python_args->user_data, NULL);
#else
    result = PyEval_CallObject(python_args->handler, python_args->user_data);
#endif

    if (result == NULL) {
        GATTLIB_LOG(GATTLIB_ERROR, "Python task handler has raised an exception.");
    }

    PyGILState_Release(d_gstate);
    return result;
}

int gattlib_mainloop_python(PyObject *handler, PyObject *user_data) {
    struct gattlib_mainloop_handler_python_args python_args = {
        .handler = handler,
        .user_data = user_data
    };
    return gattlib_mainloop(_gattlib_mainloop_handler_python, &python_args);
}
#endif
