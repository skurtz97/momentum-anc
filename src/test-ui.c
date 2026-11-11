/* Headless check that a window without a session survives row changes, which
 * used to dereference a NULL session and segfault. Needs a display, so run it
 * under xvfb-run. */
#include <adwaita.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"

static AdwSwitchRow* find_switch(GtkWidget* root, const char* title) {
	for (GtkWidget* c = gtk_widget_get_first_child(root); c != NULL;
	     c = gtk_widget_get_next_sibling(c)) {
		if (ADW_IS_SWITCH_ROW(c) &&
		    g_strcmp0(adw_preferences_row_get_title(
				  ADW_PREFERENCES_ROW(c)),
			      title) == 0) {
			return ADW_SWITCH_ROW(c);
		}
		AdwSwitchRow* found = find_switch(c, title);
		if (found != NULL) {
			return found;
		}
	}
	return NULL;
}

int main(void) {
	gtk_init();

	AdwApplication* app =
	    adw_application_new("io.github.momentum_anc.Test",
				G_APPLICATION_NON_UNIQUE);
	if (!g_application_register(G_APPLICATION(app), NULL, NULL)) {
		fprintf(stderr, "cannot register the test application\n");
		return 1;
	}
	GtkWidget* window = mc_window_new(GTK_APPLICATION(app), NULL);
	if (window == NULL) {
		fprintf(stderr, "mc_window_new returned NULL\n");
		return 1;
	}

	const char* titles[] = {"Active noise cancellation", "Adaptive",
				"Transparency"};
	for (size_t i = 0; i < G_N_ELEMENTS(titles); i++) {
		AdwSwitchRow* row = find_switch(window, titles[i]);
		if (row == NULL) {
			fprintf(stderr, "row not found: %s\n", titles[i]);
			return 1;
		}
		adw_switch_row_set_active(row, TRUE);
		adw_switch_row_set_active(row, FALSE);
	}

	gtk_window_destroy(GTK_WINDOW(window));
	g_object_unref(app);
	printf("ui: ok\n");
	return 0;
}
