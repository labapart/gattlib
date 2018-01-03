/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2017 Olivier Martin <olivier@labapart.org>
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

#ifndef __GATTLIB_INTERNAL_H__
#define __GATTLIB_INTERNAL_H__
#include <stdio.h>
#include "gattlib.h"

#include "org-bluez-adaptater1.h"
#include "org-bluez-device1.h"
#include "org-bluez-gattcharacteristic1.h"
#include "org-bluez-gattdescriptor1.h"
#include "org-bluez-gattservice1.h"

#include "bluez5/lib/uuid.h"

#define BLUEZ_VERSIONS(major, minor)	(((major) << 8) | (minor))
#define BLUEZ_VERSION					BLUEZ_VERSIONS(BLUEZ_VERSION_MAJOR, BLUEZ_VERSION_MINOR)

typedef struct {
	char* device_object_path;
	OrgBluezDevice1* device;
} gattlib_context_t;





/* define GATTLIB_DEBUG_OUTPUT_ENABLE */

#ifdef GATTLIB_DEBUG_OUTPUT_ENABLE
extern int debug_num_loops;
#endif

#ifdef GATTLIB_DEBUG_OUTPUT_ENABLE
#define DEBUG_GATTLIB(...) fprintf(stderr, __VA_ARGS__)
#define DEBUG_INC_NUMLOOPS()	debug_num_loops++; DEBUG_GATTLIB("\n++ num loops now: %i\n", debug_num_loops)
#define DEBUG_DEC_NUMLOOPS()	debug_num_loops--; DEBUG_GATTLIB("\n-- num loops now: %i\n", debug_num_loops)
#else
#define DEBUG_GATTLIB(...)
#define DEBUG_INC_NUMLOOPS()
#define DEBUG_DEC_NUMLOOPS() 
#endif

#define ERROR_GATTLIB(...) 		fprintf(stderr, __VA_ARGS__)

OrgBluezGattCharacteristic1 *get_characteristic_from_uuid(const uuid_t* uuid);

#endif
