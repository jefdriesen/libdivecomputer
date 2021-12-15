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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _WIN32
#define _WIN32_WINNT 0x0600
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <windows.h>
#endif
#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif
#include <stdlib.h>

#ifdef _WIN32
#define DC_MUTEX_INIT(mutex) InitializeCriticalSection (mutex)
#define DC_MUTEX_FREE(mutex) DeleteCriticalSection (mutex)
#define DC_MUTEX_LOCK(mutex) EnterCriticalSection (mutex)
#define DC_MUTEX_UNLOCK(mutex) LeaveCriticalSection (mutex)
#define DC_COND_INIT(cond) InitializeConditionVariable (cond)
#define DC_COND_FREE(cond)
#define DC_COND_SIGNAL(cond) WakeConditionVariable (cond)
#else
#define DC_MUTEX_INIT(mutex) pthread_mutex_init (mutex, NULL)
#define DC_MUTEX_FREE(mutex) pthread_mutex_destroy (mutex)
#define DC_MUTEX_LOCK(mutex) pthread_mutex_lock (mutex)
#define DC_MUTEX_UNLOCK(mutex) pthread_mutex_unlock (mutex)
#define DC_COND_INIT(cond) pthread_cond_init (cond, NULL)
#define DC_COND_FREE(cond) pthread_cond_destroy (cond)
#define DC_COND_SIGNAL(cond) pthread_cond_signal (cond)
#endif

#include "queue.h"

typedef struct dc_node_t dc_node_t;

struct dc_node_t {
	dc_node_t *prev;
	dc_node_t *next;
	void *data;
};

struct dc_queue_t {
	dc_node_t *head;
	dc_node_t *tail;
	dc_queue_free_t free;
#ifdef _WIN32
	CRITICAL_SECTION mutex;
	CONDITION_VARIABLE cond;
#else
	pthread_mutex_t mutex;
	pthread_cond_t cond;
#endif
};

dc_queue_t *
dc_queue_new (dc_queue_free_t free)
{
	dc_queue_t *queue = malloc (sizeof (dc_queue_t));
	if (queue == NULL)
		return NULL;

	queue->head = NULL;
	queue->tail = NULL;
	queue->free = free;

	DC_MUTEX_INIT (&queue->mutex);
	DC_COND_INIT (&queue->cond);

	return queue;
}

void
dc_queue_free (dc_queue_t *queue)
{
	if (queue == NULL)
		return;

	dc_node_t *node = queue->head;
	while (node != NULL) {
		dc_node_t *next = node->next;

		if (queue->free)
			queue->free (node->data);

		free (node);

		node = next;
	}

	DC_COND_FREE (&queue->cond);
	DC_MUTEX_FREE (&queue->mutex);
	free (queue);
}

void
dc_queue_clear (dc_queue_t *queue)
{
	if (queue == NULL)
		return;

	DC_MUTEX_LOCK (&queue->mutex);

	dc_node_t *node = queue->head;
	while (node != NULL) {
		dc_node_t *next = node->next;

		if (queue->free)
			queue->free (node->data);

		free (node);

		node = next;
	}

	DC_MUTEX_UNLOCK (&queue->mutex);
}

void *
dc_queue_push (dc_queue_t *queue, void *data)
{
	if (queue == NULL)
		return NULL;

	dc_node_t *node = malloc (sizeof (dc_node_t));
	if (node == NULL)
		return NULL;

	DC_MUTEX_LOCK (&queue->mutex);

	node->data = data;
	node->prev = queue->tail;
	node->next = NULL;
	if (queue->tail)
		queue->tail->next = node;
	else
		queue->head = node;
	queue->tail = node;

	DC_MUTEX_UNLOCK (&queue->mutex);
	DC_COND_SIGNAL (&queue->cond);

	return data;
}

void *
dc_queue_pop (dc_queue_t *queue, int timeout)
{
	void *data = NULL;

	if (queue == NULL)
		return NULL;

#ifdef _WIN32
	DWORD ts = timeout >= 0 ? (DWORD) timeout : INFINITE;
#else
	struct timespec ts = {0};
	if (timeout >= 0) {
#ifdef HAVE_CLOCK_GETTIME
		clock_gettime (CLOCK_REALTIME, &ts);
#else
		struct timeval tv = {0};
		gettimeofday (&tv, NULL);
		ts.tv_sec  = tv.tv_sec;
		ts.tv_nsec = tv.tv_usec * 1000;
#endif
		ts.tv_sec  += (timeout / 1000);
		ts.tv_nsec += (timeout % 1000) * 1000000;
	}
#endif

	DC_MUTEX_LOCK (&queue->mutex);

	while (queue->head == NULL) {
#ifdef _WIN32
		if (!SleepConditionVariableCS (&queue->cond, &queue->mutex, ts)) {
#else
		if (timeout >= 0 ?
			pthread_cond_timedwait (&queue->cond, &queue->mutex, &ts) != 0 :
			pthread_cond_wait (&queue->cond, &queue->mutex) != 0) {
#endif
			goto cleanup;
		}
	}

	dc_node_t *node = queue->head;

	queue->head = node->next;
	if (node->next)
		node->next->prev = NULL;
	else
		queue->tail = NULL;

	data = node->data;
	free (node);

cleanup:
	DC_MUTEX_UNLOCK (&queue->mutex);

	return data;
}
