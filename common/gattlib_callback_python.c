/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024, Olivier Martin <olivier@labapart.org>
 */

#include "gattlib_internal.h"


void* gattlib_python_callback_args(PyObject* python_callback, PyObject* python_args) {
	assert(python_callback != NULL);
	assert(python_args != NULL);

	struct gattlib_python_args* args = calloc(sizeof(struct gattlib_python_args), 1);
	if (args == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to allocate Python arguments for Python callback.");
		return NULL;
	}
	Py_INCREF(python_callback);
	Py_INCREF(python_args);

	args->callback = python_callback;
	args->args = python_args;
	return args;
}

