#include "bt.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <errno.h>
#include <gio/gio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int bt_open(const char* mac, char** error) {
	struct sockaddr_rc addr = {
	    .rc_family = AF_BLUETOOTH,
	    .rc_channel = BT_GAIA_CHANNEL,
	};

	*error = NULL;
	if (str2ba(mac, &addr.rc_bdaddr) < 0) {
		*error = g_strdup("bad Bluetooth address");
		return -1;
	}

	int fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	if (fd < 0) {
		*error = g_strdup(g_strerror(errno));
		return -1;
	}

	if (connect(fd, (struct sockaddr*)&addr, sizeof addr) < 0) {
		*error = g_strdup(g_strerror(errno));
		if (close(fd) != 0) {
			g_debug("close: %s", g_strerror(errno));
		}
		return -1;
	}

	return fd;
}

int bt_find_device(char* out, size_t len) {
	GError* err = NULL;
	GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
	if (!bus) {
		g_clear_error(&err);
		return -1;
	}

	GVariant* objs = g_dbus_connection_call_sync(
	    bus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
	    "GetManagedObjects", NULL, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
	    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
	g_object_unref(bus);
	if (!objs) {
		g_clear_error(&err);
		return -1;
	}

	bool found = false;
	bool found_connected = false;
	GVariant* managed = g_variant_get_child_value(objs, 0);
	const size_t n = g_variant_n_children(managed);
	for (size_t i = 0; i < n && !found_connected; i++) {
		GVariant* entry = g_variant_get_child_value(managed, i);
		GVariant* ifaces = g_variant_get_child_value(entry, 1);
		GVariant* props = g_variant_lookup_value(
		    ifaces, "org.bluez.Device1", G_VARIANT_TYPE_VARDICT);
		const char *alias = NULL, *address = NULL;

		/* the GAIA uuid is not advertised, the name is what we get */
		if (props && g_variant_lookup(props, "Alias", "&s", &alias) &&
		    alias && g_ascii_strncasecmp(alias, "MOMENTUM", 8) == 0 &&
		    g_variant_lookup(props, "Address", "&s", &address) &&
		    address && strlen(address) < len) {
			gboolean connected = FALSE;
			(void)g_variant_lookup(props, "Connected", "b",
					       &connected);
			/* prefer a headset that is already up */
			if (connected || !found) {
				g_strlcpy(out, address, len);
				found = true;
				found_connected = connected != FALSE;
			}
		}
		if (props) {
			g_variant_unref(props);
		}
		g_variant_unref(ifaces);
		g_variant_unref(entry);
	}

	g_variant_unref(managed);
	g_variant_unref(objs);
	return found ? 0 : -1;
}
