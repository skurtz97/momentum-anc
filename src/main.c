#include <adwaita.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>

#include "ui.h"

/* Pulls --device out of argv so GtkApplication sees only its own options. */
static const char* take_device(int* argc, char** argv) {
	const char* mac = NULL;
	int out = 1;

	for (int i = 1; i < *argc; i++) {
		if (strcmp(argv[i], "--device") == 0 && i + 1 < *argc) {
			mac = argv[++i];
		} else if (g_str_has_prefix(argv[i], "--device=")) {
			mac = argv[i] + strlen("--device=");
		} else {
			argv[out++] = argv[i];
		}
	}
	argv[out] = NULL;
	*argc = out;

	return mac;
}

static void on_activate(GtkApplication* app, gpointer data) {
	const char* mac = data;
	GtkWindow* window = gtk_application_get_active_window(app);

	if (window == NULL) {
		window = GTK_WINDOW(mc_window_new(app, mac));
	}
	gtk_window_present(window);
}

int main(int argc, char** argv) {
	/* without --device the window looks for the headset itself */
	const char* mac = take_device(&argc, argv);

	GtkApplication* app = GTK_APPLICATION(adw_application_new(
	    "io.github.momentum_anc", G_APPLICATION_DEFAULT_FLAGS));
	g_signal_connect(app, "activate", G_CALLBACK(on_activate), (void*)mac);
	const int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
