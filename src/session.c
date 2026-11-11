#include "session.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <glib.h>

#include "bt.h"

enum post_kind { POST_CONNECTED, POST_DISCONNECTED, POST_CHANGED };

struct post {
	mc_session* self;
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
	mc_session* s = m->self;

	g_mutex_lock(&s->lock);
	const mc_events* ev = s->ev;
	g_mutex_unlock(&s->lock);

	if (ev != NULL) { /* a stopped session drops its queued events */
		switch (m->kind) {
			case POST_CONNECTED:
				ev->connected(s->data);
				break;
			case POST_DISCONNECTED:
				ev->disconnected(s->data, m->reason);
				break;
			case POST_CHANGED:
				ev->changed(s->data, m->field, m->value);
				break;
		}
	}

	g_free(m->reason);
	g_free(m);
	return G_SOURCE_REMOVE;
}

static void post(mc_session* s, struct post m) {
	m.self = s;
	g_idle_add(deliver, g_memdup2(&m, sizeof m));
}

static void handle_frame(mc_session* s, const gaia_frame* f) {
	gaia_field fields[2];
	int values[2];
	size_t n = gaia_decode(f, fields, values, G_N_ELEMENTS(fields));

	for (size_t i = 0; i < n; i++) {
		post(s, (struct post){.kind = POST_CHANGED,
				      .field = fields[i],
				      .value = values[i]});
	}
}

static void request(mc_session* s, gaia_field field) {
	uint8_t frame[16];
	const size_t n = gaia_get_field(field, frame, sizeof frame);

	g_mutex_lock(&s->lock);
	if (s->fd >= 0 && n > 0) {
		(void)!write_all(s->fd, frame, n);
	}
	g_mutex_unlock(&s->lock);
}

static gpointer thread_main(gpointer p) {
	mc_session* s = p;
	uint8_t buf[512];
	size_t have = 0;

	char* error = NULL;
	const int fd = bt_open(s->mac, &error);
	if (fd < 0) {
		post(s,
		     (struct post){.kind = POST_DISCONNECTED, .reason = error});
		return NULL;
	}

	g_mutex_lock(&s->lock);
	s->fd = fd;
	g_mutex_unlock(&s->lock);

	post(s, (struct post){.kind = POST_CONNECTED});
	for (int f = GAIA_FIELD_BATTERY; f <= GAIA_FIELD_TRANSPARENCY; f++) {
		request(s, (gaia_field)f);
	}

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
	return NULL;
}

mc_session* mc_session_new(const mc_events* ev, void* data, const char* mac) {
	mc_session* s = g_new0(mc_session, 1);
	s->ev = g_memdup2(ev, sizeof *ev); /* callers pass a compound literal */
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
	for (int f = GAIA_FIELD_BATTERY; f <= GAIA_FIELD_TRANSPARENCY; f++) {
		request(s, (gaia_field)f);
	}
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
