/* Headless check that the window survives row changes with no link to the
 * headset, which used to dereference a NULL session and segfault. The bogus
 * address keeps it from ever connecting. Needs a display: xvfb-run. */
#include <adwaita.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"

static AdwSwitchRow* find_switch(GtkWidget* root, const char* title) {
	for (GtkWidget* c = gtk_widget_get_first_child(root); c != NULL;
	     c = gtk_widget_get_next_sibling(c)) {
		if (ADW_IS_SWITCH_ROW(c) &&
		    g_strcmp0(
			adw_preferences_row_get_title(ADW_PREFERENCES_ROW(c)),
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

static GtkScale* find_scale(GtkWidget* root) {
	for (GtkWidget* c = gtk_widget_get_first_child(root); c != NULL;
	     c = gtk_widget_get_next_sibling(c)) {
		if (GTK_IS_SCALE(c)) {
			return GTK_SCALE(c);
		}
		GtkScale* found = find_scale(c);
		if (found != NULL) {
			return found;
		}
	}
	return NULL;
}

int main(void) {
	gtk_init();

	AdwApplication* app = adw_application_new("io.github.momentum_anc.Test",
						  G_APPLICATION_NON_UNIQUE);
	if (!g_application_register(G_APPLICATION(app), NULL, NULL)) {
		fprintf(stderr, "cannot register the test application\n");
		return 1;
	}
	GtkWidget* window = mc_window_new(GTK_APPLICATION(app),
					  "00:00:00:00:00:00");
	if (window == NULL) {
		fprintf(stderr, "mc_window_new returned NULL\n");
		return 1;
	}

	const char* titles[] = {"Active noise cancellation", "Adaptive",
				"Transparency", "Bass boost"};
	for (size_t i = 0; i < G_N_ELEMENTS(titles); i++) {
		AdwSwitchRow* row = find_switch(window, titles[i]);
		if (row == NULL) {
			fprintf(stderr, "row not found: %s\n", titles[i]);
			return 1;
		}
		adw_switch_row_set_active(row, TRUE);
		adw_switch_row_set_active(row, FALSE);
	}

	GtkScale* scale = find_scale(window);
	if (scale == NULL) {
		fprintf(stderr, "no equalizer slider found\n");
		return 1;
	}
	gtk_range_set_value(GTK_RANGE(scale), -3.5);
	gtk_range_set_value(GTK_RANGE(scale), 0);

	gtk_window_destroy(GTK_WINDOW(window));
	g_object_unref(app);
	printf("ui: ok\n");
	return 0;
}
