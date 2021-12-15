/*
 * libdivecomputer
 *
 * Copyright (C) 2021 Jef Driesen
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

#ifndef DC_QUEUE_H
#define DC_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct dc_queue_t dc_queue_t;

typedef void (*dc_queue_free_t) (void *data);

dc_queue_t *
dc_queue_new (dc_queue_free_t free);

void
dc_queue_free (dc_queue_t *queue);

void
dc_queue_clear (dc_queue_t *queue);

void *
dc_queue_push (dc_queue_t *queue, void *item);

void *
dc_queue_pop (dc_queue_t *queue, int timeout);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* DC_QUEUE_H */
