#include "ui.h"

#include <adwaita.h>
#include <stdbool.h>

#include "gaia.h"
#include "session.h"

struct mc_window {
	AdwWindowTitle* title;
	AdwSwitchRow* anc;
	AdwSwitchRow* adaptive;
	AdwSwitchRow* transparency;
	GtkLabel* battery;
	mc_session* session;
	char* mac;
	bool syncing; /* true while the headset's values are pushed into the
			 rows */
};

static void on_connected(void* data) {
	struct mc_window* w = data;

	adw_window_title_set_subtitle(w->title, "Connected");
	mc_session_refresh(w->session);
}

static void on_disconnected(void* data, const char* reason) {
	struct mc_window* w = data;

	g_autofree char* text = g_strdup_printf("Disconnected: %s", reason);
	adw_window_title_set_subtitle(w->title, text);
	w->syncing = true;
	adw_switch_row_set_active(w->anc, false);
	adw_switch_row_set_active(w->adaptive, false);
	gtk_widget_set_sensitive(GTK_WIDGET(w->anc), false);
	gtk_widget_set_sensitive(GTK_WIDGET(w->adaptive), false);
	gtk_widget_set_sensitive(GTK_WIDGET(w->transparency), false);
	w->syncing = false;
}

static void on_changed(void* data, gaia_field field, int value) {
	struct mc_window* w = data;

	if (!mc_session_connected(w->session)) {
		return;
	}
	gtk_widget_set_sensitive(GTK_WIDGET(w->anc), true);
	gtk_widget_set_sensitive(GTK_WIDGET(w->adaptive), true);
	gtk_widget_set_sensitive(GTK_WIDGET(w->transparency), true);

	w->syncing = true;
	switch (field) {
		case GAIA_FIELD_BATTERY: {
			g_autofree char* text = g_strdup_printf("%d%%", value);
			gtk_label_set_text(w->battery, text);
			break;
		}
		case GAIA_FIELD_ANC:
			adw_switch_row_set_active(w->anc, value != 0);
			break;
		case GAIA_FIELD_ADAPTIVE:
			adw_switch_row_set_active(w->adaptive, value != 0);
			break;
		case GAIA_FIELD_TRANSPARENCY:
			adw_switch_row_set_active(w->transparency, value != 0);
			break;
	}
	w->syncing = false;
}

static void send_field(struct mc_window* w, gaia_field field, int value) {
	if (w->syncing) {
		return;
	}
	/* no --device match leaves the window without a session */
	if (w->session == NULL || !mc_session_set(w->session, field, value)) {
		adw_window_title_set_subtitle(w->title, "Not connected");
	}
}

static void on_anc_toggled(GObject* o, GParamSpec* ps, gpointer data) {
	(void)ps;
	send_field(data, GAIA_FIELD_ANC,
		   adw_switch_row_get_active(ADW_SWITCH_ROW(o)) ? 1 : 0);
}

static void on_adaptive_toggled(GObject* o, GParamSpec* ps, gpointer data) {
	(void)ps;
	send_field(data, GAIA_FIELD_ADAPTIVE,
		   adw_switch_row_get_active(ADW_SWITCH_ROW(o)) ? 1 : 0);
}

static void on_transparency_toggled(GObject* o, GParamSpec* ps, gpointer data) {
	(void)ps;
	send_field(data, GAIA_FIELD_TRANSPARENCY,
		   adw_switch_row_get_active(ADW_SWITCH_ROW(o)) ? 1 : 0);
}

static void on_reconnect(GtkButton* b, gpointer data) {
	struct mc_window* w = data;

	(void)b;
	if (w->mac == NULL) {
		return;
	}
	mc_session_stop(w->session);
	w->syncing = true;
	adw_switch_row_set_active(w->anc, false);
	adw_switch_row_set_active(w->adaptive, false);
	w->syncing = false;
	adw_window_title_set_subtitle(w->title, "Connecting…");
	w->session = mc_session_new(
	    &(mc_events){
		.connected = on_connected,
		.disconnected = on_disconnected,
		.changed = on_changed,
	    },
	    w, w->mac);
}

static GtkWidget* build_content(struct mc_window* w) {
	AdwPreferencesPage* page =
	    ADW_PREFERENCES_PAGE(adw_preferences_page_new());
	AdwPreferencesGroup* group =
	    ADW_PREFERENCES_GROUP(adw_preferences_group_new());
	AdwActionRow* battery_row = ADW_ACTION_ROW(adw_action_row_new());

	adw_preferences_group_set_title(group, "Noise control");

	w->anc = ADW_SWITCH_ROW(adw_switch_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(w->anc),
				      "Active noise cancellation");
	g_signal_connect(w->anc, "notify::active", G_CALLBACK(on_anc_toggled),
			 w);
	adw_preferences_group_add(group, GTK_WIDGET(w->anc));

	w->adaptive = ADW_SWITCH_ROW(adw_switch_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(w->adaptive),
				      "Adaptive");
	adw_action_row_set_subtitle(ADW_ACTION_ROW(w->adaptive),
				    "Follows the noise around you");
	g_signal_connect(w->adaptive, "notify::active",
			 G_CALLBACK(on_adaptive_toggled), w);
	adw_preferences_group_add(group, GTK_WIDGET(w->adaptive));

	w->transparency = ADW_SWITCH_ROW(adw_switch_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(w->transparency),
				      "Transparency");
	adw_action_row_set_subtitle(ADW_ACTION_ROW(w->transparency),
				    "Passes the room through");
	g_signal_connect(w->transparency, "notify::active",
			 G_CALLBACK(on_transparency_toggled), w);
	adw_preferences_group_add(group, GTK_WIDGET(w->transparency));

	w->battery = GTK_LABEL(gtk_label_new("—"));
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(battery_row),
				      "Battery");
	adw_action_row_add_suffix(battery_row, GTK_WIDGET(w->battery));
	adw_preferences_group_add(group, GTK_WIDGET(battery_row));

	adw_preferences_page_add(page, group);
	return GTK_WIDGET(page);
}

static void on_destroy(GtkWidget* widget, gpointer data) {
	(void)widget;
	struct mc_window* w = data;

	if (w->session != NULL) {
		mc_session_stop(w->session);
	}
	g_free(w->mac);
	g_free(w);
}
GtkWidget* mc_window_new(GtkApplication* app, const char* mac) {
	GtkWidget* window = adw_application_window_new(app);
	AdwHeaderBar* header = ADW_HEADER_BAR(adw_header_bar_new());
	AdwToolbarView* view = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());

	struct mc_window* w = g_new0(struct mc_window, 1);
	w->mac = g_strdup(mac);
	w->title =
	    ADW_WINDOW_TITLE(adw_window_title_new("MOMENTUM 4", "Connecting…"));
	adw_header_bar_set_title_widget(header, GTK_WIDGET(w->title));

	GtkWidget* reconnect =
	    gtk_button_new_from_icon_name("view-refresh-symbolic");
	gtk_widget_set_tooltip_text(reconnect, "Reconnect");
	g_signal_connect(reconnect, "clicked", G_CALLBACK(on_reconnect), w);
	adw_header_bar_pack_start(header, reconnect);

	adw_toolbar_view_add_top_bar(view, GTK_WIDGET(header));
	adw_toolbar_view_set_content(view, build_content(w));
	adw_application_window_set_content(ADW_APPLICATION_WINDOW(window),
					   GTK_WIDGET(view));

	if (w->mac != NULL) {
		w->session = mc_session_new(
		    &(mc_events){
			.connected = on_connected,
			.disconnected = on_disconnected,
			.changed = on_changed,
		    },
		    w, w->mac);
	} else {
		adw_window_title_set_subtitle(
		    w->title, "No paired Momentum found (use --device)");
		gtk_widget_set_sensitive(GTK_WIDGET(w->anc), false);
		gtk_widget_set_sensitive(GTK_WIDGET(w->adaptive), false);
		gtk_widget_set_sensitive(GTK_WIDGET(w->transparency), false);
	}

	g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), w);
	return window;
}
