/* 
 * libdivecomputer
 * 
 * Copyright (C) 2008 Jef Driesen
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

#include <stdio.h>	// fopen, fwrite, fclose

#include "oceanic_veo250.h"
#include "utils.h"

device_status_t
test_dump_memory (const char* name, const char* filename)
{
	unsigned char data[OCEANIC_VEO250_MEMORY_SIZE] = {0};
	device_t *device = NULL;

	message ("oceanic_veo250_device_open\n");
	device_status_t rc = oceanic_veo250_device_open (&device, name);
	if (rc != DEVICE_STATUS_SUCCESS) {
		WARNING ("Error opening serial port.");
		return rc;
	}

	message ("device_dump\n");
	unsigned int nbytes = 0;
	rc = device_dump (device, data, sizeof (data), &nbytes);
	if (rc != DEVICE_STATUS_SUCCESS) {
		WARNING ("Cannot read memory.");
		device_close (device);
		return rc;
	}

	message ("Dumping data\n");
	FILE* fp = fopen (filename, "wb");
	if (fp != NULL) {
		fwrite (data, sizeof (unsigned char), nbytes, fp);
		fclose (fp);
	}

	message ("device_foreach\n");
	rc = device_foreach (device, NULL, NULL);
	if (rc != DEVICE_STATUS_SUCCESS) {
		WARNING ("Cannot read dives.");
		device_close (device);
		return rc;
	}

	message ("device_close\n");
	rc = device_close (device);
	if (rc != DEVICE_STATUS_SUCCESS) {
		WARNING ("Cannot close device.");
		return rc;
	}

	return DEVICE_STATUS_SUCCESS;
}


const char*
errmsg (device_status_t rc)
{
	switch (rc) {
	case DEVICE_STATUS_SUCCESS:
		return "Success";
	case DEVICE_STATUS_UNSUPPORTED:
		return "Unsupported operation";
	case DEVICE_STATUS_TYPE_MISMATCH:
		return "Device type mismatch";
	case DEVICE_STATUS_ERROR:
		return "Generic error";
	case DEVICE_STATUS_IO:
		return "Input/output error";
	case DEVICE_STATUS_MEMORY:
		return "Memory error";
	case DEVICE_STATUS_PROTOCOL:
		return "Protocol error";
	case DEVICE_STATUS_TIMEOUT:
		return "Timeout";
	default:
		return "Unknown error";
	}
}

int main(int argc, char *argv[])
{
	message_set_logfile ("VEO250.LOG");

#ifdef _WIN32
	const char* name = "COM1";
#else
	const char* name = "/dev/ttyS0";
#endif

	if (argc > 1) {
		name = argv[1];
	}

	message ("DEVICE=%s\n", name);

	device_status_t a = test_dump_memory (name, "VEO250.DMP");

	message ("\nSUMMARY\n");
	message ("-------\n");
	message ("test_dump_memory: %s\n", errmsg (a));

	message_set_logfile (NULL);

	return 0;
}
