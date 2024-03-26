/*
 * SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
 *
 * Copyright (c) 2024, Olivier Martin <olivier@labapart.org>
 */

#include <syslog.h>

#include "gattlib_internal.h"

static PyObject* m_logging_func;

void gattlib_log_init(PyObject* logging_func) {
	m_logging_func = logging_func;
}

void gattlib_log(int level, const char *format, ...) {
	va_list args;
	va_start(args, format);

	if (m_logging_func == NULL) {
		FILE *stream = stdout;

		if (level == GATTLIB_ERROR) {
			stream = stderr;
		}

		vfprintf(stream, format, args);
		fprintf(stream, "\n");
	} else {
		PyGILState_STATE d_gstate;
		PyObject *result;
		char string[400];

		vsnprintf(string, sizeof(string), format, args);

		d_gstate = PyGILState_Ensure();

		PyObject *arglist = Py_BuildValue("Is", level, string);
#if PYTHON_VERSION >= PYTHON_VERSIONS(3, 9)
		result = PyObject_Call(m_logging_func, arglist, NULL);
#else
		result = PyEval_CallObject(m_logging_func, arglist);
#endif
		if (result == NULL) {
			// We do not call GATTLIB_LOG(GATTLIB_ERROR, "") in case of error to avoid recursion
			fprintf(stderr, "Failed to call Python logging function for this logging: %s\n", format);
			PyErr_Print();
		}

		Py_DECREF(arglist);

		PyGILState_Release(d_gstate);
	}

	va_end(args);
}
