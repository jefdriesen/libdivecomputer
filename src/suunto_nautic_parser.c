/*
 * libdivecomputer
 *
 * Copyright (C) 2026 Jef Driesen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA
 */

#include <stdlib.h>
#include <string.h>

#include <libdivecomputer/units.h>

#include "suunto_nautic.h"
#include "context-private.h"
#include "parser-private.h"
#include "checksum.h"
#include "array.h"

typedef struct suunto_nautic_parser_t {
	dc_parser_t base;
} suunto_nautic_parser_t;

static dc_status_t suunto_nautic_parser_get_datetime (dc_parser_t *abstract, dc_datetime_t *datetime);
static dc_status_t suunto_nautic_parser_get_field (dc_parser_t *abstract, dc_field_type_t type, unsigned int flags, void *value);
static dc_status_t suunto_nautic_parser_samples_foreach (dc_parser_t *abstract, dc_sample_callback_t callback, void *userdata);

static const dc_parser_vtable_t suunto_nautic_parser_vtable = {
	sizeof(suunto_nautic_parser_t),
	DC_FAMILY_SUUNTO_NAUTIC,
	NULL, /* set_clock */
	NULL, /* set_atmospheric */
	NULL, /* set_density */
	suunto_nautic_parser_get_datetime, /* datetime */
	suunto_nautic_parser_get_field, /* fields */
	suunto_nautic_parser_samples_foreach, /* samples_foreach */
	NULL /* destroy */
};

dc_status_t
suunto_nautic_parser_create (dc_parser_t **out, dc_context_t *context, const unsigned char data[], size_t size)
{
	suunto_nautic_parser_t *parser = NULL;

	if (out == NULL)
		return DC_STATUS_INVALIDARGS;

	// Allocate memory.
	parser = (suunto_nautic_parser_t *) dc_parser_allocate (context, &suunto_nautic_parser_vtable, data, size);
	if (parser == NULL) {
		ERROR (context, "Failed to allocate memory.");
		return DC_STATUS_NOMEMORY;
	}

	// Set the default values.
	// TODO

	*out = (dc_parser_t *) parser;

	return DC_STATUS_SUCCESS;
}

static dc_status_t
suunto_nautic_parser_get_datetime (dc_parser_t *abstract, dc_datetime_t *datetime)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	suunto_nautic_parser_t *parser = (suunto_nautic_parser_t *) abstract;
	const unsigned char *data = abstract->data;
	unsigned int size = abstract->size;

	// TODO

	return DC_STATUS_SUCCESS;
}

static dc_status_t
suunto_nautic_parser_get_field (dc_parser_t *abstract, dc_field_type_t type, unsigned int flags, void *value)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	suunto_nautic_parser_t *parser = (suunto_nautic_parser_t *) abstract;
	const unsigned char *data = abstract->data;
	unsigned int size = abstract->size;

	dc_salinity_t *water = (dc_salinity_t *) value;
	dc_gasmix_t *gasmix = (dc_gasmix_t *) value;
	dc_tank_t *tank = (dc_tank_t *) value;
	dc_decomodel_t *decomodel = (dc_decomodel_t *) value;
	dc_location_t *location = (dc_location_t *) value;

	if (value) {
		switch (type) {
		// TODO
#if 0
		case DC_FIELD_DIVETIME:
			*((unsigned int *) value) = 0;
			break;
		case DC_FIELD_MAXDEPTH:
			*((double *) value) = 0;
			break;
		case DC_FIELD_AVGDEPTH:
			*((double *) value) = 0;
			break;
		case DC_FIELD_TEMPERATURE_MINIMUM:
			*((double *) value) = 0;
			break;
		case DC_FIELD_ATMOSPHERIC:
			*((double *) value) = 0;
			break;
		case DC_FIELD_SALINITY:
			water->type = DC_WATER_FRESH;
			water->density = 0;
			break;
		case DC_FIELD_DIVEMODE:
			break;
		case DC_FIELD_GASMIX_COUNT:
			*((unsigned int *) value) = 0;
			break;
		case DC_FIELD_GASMIX:
			gasmix->usage = DC_USAGE_NONE;
			gasmix->helium = 0;
			gasmix->oxygen = 0.21;
			gasmix->nitrogen = 1.0 - gasmix->oxygen - gasmix->helium;
			break;
		case DC_FIELD_TANK_COUNT:
			*((unsigned int *) value) = 0;
			break;
		case DC_FIELD_TANK:
			tank->type = DC_TANKVOLUME_METRIC;
			tank->volume = 0;
			tank->workpressure  = 0;
			tank->beginpressure = 0;
			tank->endpressure   = 0;
			tank->gasmix = DC_GASMIX_UNKNOWN;
			tank->usage = DC_USAGE_NONE;
			break;
		case DC_FIELD_DECOMODEL:
			decomodel->type = DC_DECOMODEL_BUHLMANN;
			decomodel->conservatism = 0;
			decomodel->params.gf.low = 100;
			decomodel->params.gf.high = 100;
			break;
#endif
		default:
			return DC_STATUS_UNSUPPORTED;
		}
	}

	return DC_STATUS_SUCCESS;
}

static dc_status_t
suunto_nautic_parser_samples_foreach (dc_parser_t *abstract, dc_sample_callback_t callback, void *userdata)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	suunto_nautic_parser_t *parser = (suunto_nautic_parser_t *) abstract;
	const unsigned char *data = abstract->data;
	unsigned int size = abstract->size;

	// TODO
#if 0
	unsigned int time = 0;
	unsigned int offset = 0;
	while (offset + 1 <= size) {
		dc_sample_value_t sample = {0};

		sample.time = time;
		if (callback) callback(DC_SAMPLE_DEPTH, &sample, userdata);

		sample.depth = 0.0;
		if (callback) callback(DC_SAMPLE_DEPTH, &sample, userdata);

		offset += 1;
	}
#endif

	return DC_STATUS_SUCCESS;
}
