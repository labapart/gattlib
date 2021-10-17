/*
 * SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
 *
 * Copyright (c) 2021, Olivier Martin <olivier@labapart.org>
 */

#include "gattlib_internal.h"

void gattlib_log(int level, const char *format, ...) {
	va_list args;
	FILE *stream = stdout;

	if (level == GATTLIB_ERROR) {
		stream = stderr;
	}

	va_start(args, format);
	vfprintf(stream, format, args);
	fprintf(stream, "\n");
	va_end(args);
}
