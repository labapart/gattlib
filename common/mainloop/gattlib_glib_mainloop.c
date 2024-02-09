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

int gattlib_mainloop(void* (*task)(void* arg), void *arg) {
    GThread *task_thread = g_thread_new("gattlib_task", task, arg);

    m_main_loop = g_main_loop_new(NULL, FALSE);

    g_main_loop_run(m_main_loop);
    g_main_loop_unref(m_main_loop);

    g_thread_join(task_thread);

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
