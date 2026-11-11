#ifndef SESSION_H
#define SESSION_H

#include <stdbool.h>

#include "gaia.h"

typedef struct mc_session mc_session;

typedef struct mc_events {
	void (*connected)(void* data);
	void (*disconnected)(void* data, const char* reason);
	/* runs on the GTK main loop */
	void (*changed)(void* data, gaia_field field, int value);
} mc_events;

/* Connects on a worker thread; events arrive on the GTK main loop. `ev` must
 * outlive the session: mc_session_stop() only stops queueing new events. A
 * NULL mac makes the worker keep looking for a paired MOMENTUM headset. */
mc_session* mc_session_new(const mc_events* ev, void* data, const char* mac);

/* Stops the worker and closes the socket. The worker frees itself, so the
 * handle must not be used afterwards; queued events are dropped. */
void mc_session_stop(mc_session* s);

/* Asks the headset for every field the UI shows. */
void mc_session_refresh(mc_session* s);

/* Queues a write; false when not connected. */
bool mc_session_set(mc_session* s, gaia_field field, int value);

/* True while the socket is up. */
bool mc_session_connected(mc_session* s);

#endif // SESSION_H
