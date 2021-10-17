/*
 * SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
 *
 * Copyright (c) 2021, Olivier Martin <olivier@labapart.org>
 */

#include <syslog.h>

#include "gattlib_internal.h"

static const int m_gattlib_log_level_to_syslog[] = {
	LOG_ERR, LOG_WARNING, LOG_INFO, LOG_DEBUG
};

void gattlib_log(int level, const char *format, ...) {
	va_list args;

	va_start(args, format);
	vsyslog(m_gattlib_log_level_to_syslog[level], format, args);
	va_end(args);
}
