#include "session.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <glib.h>

#include "bt.h"

enum post_kind { POST_CONNECTED, POST_DISCONNECTED, POST_CHANGED };

struct post {
	/* snapshotted when queued, so a stopped or freed session is never
	 * touched from the main loop */
	const mc_events* ev;
	void* data;
	enum post_kind kind;
	gaia_field field;
	int value;
	char* reason;
};

struct mc_session {
	const mc_events* ev;
	void* data;
	char* mac;
	GThread* thread;
	GMutex lock;
	int fd; /* -1 until connected; guarded by lock */
	bool stop;
	/* the 0x1102 reply carries a gain but no band: pair each one with the
	 * request that produced it, in order */
	gaia_field eq_queue[GAIA_EQ_BANDS];
	size_t eq_waiting; /* guarded by lock */
};

static bool write_all(int fd, const uint8_t* buf, size_t len) {
	size_t off = 0;

	while (off < len) {
		const ssize_t n = write(fd, buf + off, len - off);
		if (n <= 0) {
			return false;
		}
		off += (size_t)n;
	}
	return true;
}

static bool running(mc_session* s) {
	g_mutex_lock(&s->lock);
	const bool stop = s->stop;
	g_mutex_unlock(&s->lock);
	return !stop;
}

static gboolean deliver(gpointer p) {
	struct post* m = p;

	if (m->ev != NULL) { /* a stopped session drops its queued events */
		switch (m->kind) {
			case POST_CONNECTED:
				m->ev->connected(m->data);
				break;
			case POST_DISCONNECTED:
				m->ev->disconnected(m->data, m->reason);
				break;
			case POST_CHANGED:
				m->ev->changed(m->data, m->field, m->value);
				break;
		}
	}

	g_free(m->reason);
	g_free(m);
	return G_SOURCE_REMOVE;
}

static void post(mc_session* s, struct post m) {
	g_mutex_lock(&s->lock);
	m.ev = s->ev;
	g_mutex_unlock(&s->lock);
	m.data = s->data;

	if (m.ev == NULL) {
		g_free(m.reason);
		return;
	}
	g_idle_add(deliver, g_memdup2(&m, sizeof m));
}

static void handle_frame(mc_session* s, const gaia_frame* f) {
	if (f->cmd == GAIA_CMD_EQ_BAND_REPLY) {
		gaia_field field = GAIA_FIELD_BATTERY;

		g_mutex_lock(&s->lock);
		const bool queued = s->eq_waiting > 0;
		if (queued) {
			field = s->eq_queue[0];
			s->eq_waiting--;
			memmove(s->eq_queue, s->eq_queue + 1,
				s->eq_waiting * sizeof s->eq_queue[0]);
		}
		g_mutex_unlock(&s->lock);

		if (queued && f->payload_len >= 1) {
			post(s, (struct post){.kind = POST_CHANGED,
					      .field = field,
					      .value = (int8_t)f->payload[0]});
		}
		return;
	}

	gaia_field fields[2];
	int values[2];
	size_t n = gaia_decode(f, fields, values, G_N_ELEMENTS(fields));

	for (size_t i = 0; i < n; i++) {
		post(s, (struct post){.kind = POST_CHANGED,
				      .field = fields[i],
				      .value = values[i]});
	}
}

static const gaia_field readouts[] = {
    GAIA_FIELD_BATTERY,   GAIA_FIELD_ANC,      GAIA_FIELD_ADAPTIVE,
    GAIA_FIELD_TRANSPARENCY, GAIA_FIELD_BASS,  GAIA_FIELD_FIRMWARE,
    GAIA_FIELD_EQ_BAND0,  GAIA_FIELD_EQ_BAND1, GAIA_FIELD_EQ_BAND2,
    GAIA_FIELD_EQ_BAND3,  GAIA_FIELD_EQ_BAND4,
};

static void request(mc_session* s, gaia_field field) {
	uint8_t frame[16];
	const size_t n = gaia_get_field(field, frame, sizeof frame);

	g_mutex_lock(&s->lock);
	if (s->fd >= 0 && n > 0) {
		if (field >= GAIA_FIELD_EQ_BAND0 &&
		    field <= GAIA_FIELD_EQ_BAND4 &&
		    s->eq_waiting < GAIA_EQ_BANDS) {
			s->eq_queue[s->eq_waiting++] = field;
		}
		(void)!write_all(s->fd, frame, n);
	}
	g_mutex_unlock(&s->lock);
}

static void read_all(mc_session* s) {
	for (size_t i = 0; i < G_N_ELEMENTS(readouts); i++) {
		request(s, readouts[i]);
	}
}

/* sleeps in slices so a stop request is noticed quickly */
static bool pause_ms(mc_session* s, int ms) {
	for (int waited = 0; waited < ms; waited += 250) {
		if (!running(s)) {
			return false;
		}
		g_usleep(250 * 1000);
	}
	return running(s);
}

static gpointer thread_main(gpointer p) {
	mc_session* s = p;
	uint8_t buf[512];

	while (running(s)) {
		if (s->mac == NULL) {
			char found[18] = "";
			if (bt_find_device(found, sizeof found) != 0) {
				/* the headset may be switched on much later */
				if (!pause_ms(s, 5000)) {
					break;
				}
				continue;
			}
			s->mac = g_strdup(found); /* only this thread writes it */
		}

		char* error = NULL;
		const int fd = bt_open(s->mac, &error);
		if (fd < 0) {
			post(s, (struct post){.kind = POST_DISCONNECTED,
					      .reason = error});
			if (!pause_ms(s, 5000)) {
				break;
			}
			continue;
		}

		size_t have = 0;
		g_mutex_lock(&s->lock);
		s->fd = fd;
		s->eq_waiting = 0;
		g_mutex_unlock(&s->lock);

		post(s, (struct post){.kind = POST_CONNECTED});
		read_all(s);

		while (running(s)) {
			const ssize_t n = read(fd, buf + have, sizeof buf - have);
			if (n <= 0) {
				break;
			}
			have += (size_t)n;

			for (;;) {
				gaia_frame f;
				const int used = gaia_parse(buf, have, &f);
				if (used <= 0) {
					break; /* keep the tail, it may start a frame */
				}
				handle_frame(s, &f);
				have -= (size_t)used;
				memmove(buf, buf + used, have);
			}

			if (have == sizeof buf) {
				have = 0; /* junk that never resynced */
			}
		}

		g_mutex_lock(&s->lock);
		s->fd = -1;
		g_mutex_unlock(&s->lock);
		if (close(fd) != 0) {
			g_debug("close: %s", g_strerror(errno));
		}

		post(s, (struct post){.kind = POST_DISCONNECTED,
				      .reason = g_strdup(have ? "bad stream"
							      : "headset went away")});
		if (!pause_ms(s, 5000)) {
			break;
		}
	}

	/* the worker owns the struct once it stops: the events block belongs to
	 * the caller, and idle callbacks carry their own copy of what they need */
	g_mutex_clear(&s->lock);
	g_free(s->mac);
	g_free(s);

	return NULL;
}

mc_session* mc_session_new(const mc_events* ev, void* data, const char* mac) {
	mc_session* s = g_new0(mc_session, 1);
	s->ev = ev; /* the caller keeps it alive until the worker stops */
	s->data = data;
	s->mac = g_strdup(mac);
	s->fd = -1;
	g_mutex_init(&s->lock);
	s->thread = g_thread_new("gaia", thread_main, s);

	return s;
}

void mc_session_stop(mc_session* s) {
	g_mutex_lock(&s->lock);
	s->stop = true;
	s->ev = NULL; /* the caller may free its data after this */
	const int fd = s->fd;
	g_mutex_unlock(&s->lock);

	if (fd >= 0) {
		shutdown(fd, SHUT_RDWR); /* unblocks the reader */
	}
	g_thread_unref(s->thread);
}

void mc_session_refresh(mc_session* s) {
	read_all(s);
}

bool mc_session_connected(mc_session* s) {
	g_mutex_lock(&s->lock);
	const bool up = s->fd >= 0;
	g_mutex_unlock(&s->lock);
	return up;
}

bool mc_session_set(mc_session* s, gaia_field field, int value) {
	uint8_t frame[16];
	const size_t n = gaia_set_field(field, value, frame, sizeof frame);
	bool ok = false;

	g_mutex_lock(&s->lock);
	if (s->fd >= 0 && n > 0) {
		ok = write_all(s->fd, frame, n);
	}
	g_mutex_unlock(&s->lock);
	return ok;
}
