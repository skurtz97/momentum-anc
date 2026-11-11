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
	AdwSwitchRow* bass;
	GtkScale* eq[GAIA_EQ_BANDS];
	GtkLabel* battery;
	GtkLabel* firmware;
	mc_session* session;
	char* mac;
	bool syncing; /* true while the headset's values are pushed into the
			 rows */
	mc_events events; /* the session keeps a pointer to this */
};

/* Everything that only makes sense with a link to the headset. */
static void set_rows(struct mc_window* w, bool sensitive) {
	GtkWidget* rows[] = {GTK_WIDGET(w->anc), GTK_WIDGET(w->adaptive),
			     GTK_WIDGET(w->transparency),
			     GTK_WIDGET(w->bass)};

	for (size_t i = 0; i < G_N_ELEMENTS(rows); i++) {
		gtk_widget_set_sensitive(rows[i], sensitive);
	}
	for (size_t i = 0; i < GAIA_EQ_BANDS; i++) {
		gtk_widget_set_sensitive(GTK_WIDGET(w->eq[i]), sensitive);
	}
}

static void on_connected(void* data) {
	struct mc_window* w = data;

	adw_window_title_set_subtitle(w->title, "Connected");
	set_rows(w, true);
	mc_session_refresh(w->session);
}

static void on_disconnected(void* data, const char* reason) {
	struct mc_window* w = data;

	g_autofree char* text = g_strdup_printf("Disconnected: %s", reason);
	adw_window_title_set_subtitle(w->title, text);
	set_rows(w, false);
	w->syncing = true;
	adw_switch_row_set_active(w->anc, false);
	adw_switch_row_set_active(w->adaptive, false);
	adw_switch_row_set_active(w->transparency, false);
	adw_switch_row_set_active(w->bass, false);
	w->syncing = false;
}

static void on_changed(void* data, gaia_field field, int value) {
	struct mc_window* w = data;

	w->syncing = true;
	switch (field) {
		case GAIA_FIELD_BATTERY: {
			g_autofree char* text = g_strdup_printf("%d%%", value);
			gtk_label_set_text(w->battery, text);
			break;
		}
		case GAIA_FIELD_FIRMWARE: {
			g_autofree char* text = g_strdup_printf(
			    "%d.%d.%d", value / 10000, value / 100 % 100,
			    value % 100);
			gtk_label_set_text(w->firmware, text);
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
		case GAIA_FIELD_BASS:
			adw_switch_row_set_active(w->bass, value != 0);
			break;
		case GAIA_FIELD_EQ_BAND0:
		case GAIA_FIELD_EQ_BAND1:
		case GAIA_FIELD_EQ_BAND2:
		case GAIA_FIELD_EQ_BAND3:
		case GAIA_FIELD_EQ_BAND4:
			gtk_range_set_value(GTK_RANGE(w->eq[field -
							     GAIA_FIELD_EQ_BAND0]),
					    value / 10.0);
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

/* (Re)starts the worker; the session stops itself when it is done. */
static void start_session(struct mc_window* w) {
	w->events = (mc_events){
	    .connected = on_connected,
	    .disconnected = on_disconnected,
	    .changed = on_changed,
	};
	w->session = mc_session_new(&w->events, w, w->mac);
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
	start_session(w);
}

static void on_bass_toggled(GObject* o, GParamSpec* ps, gpointer data) {
	(void)ps;
	send_field(data, GAIA_FIELD_BASS,
		   adw_switch_row_get_active(ADW_SWITCH_ROW(o)) ? 1 : 0);
}

static void on_eq_changed(GtkRange* range, gpointer data) {
	struct mc_window* w = data;
	const int band =
	    GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "band"));
	const double db = gtk_range_get_value(range);

	/* the slider steps by 0.5 dB, the wire wants millibel */
	send_field(w, GAIA_FIELD_EQ_BAND0 + band,
		   (int)(db < 0 ? db * 10 - 0.5 : db * 10 + 0.5));
}

static AdwActionRow* build_eq_row(struct mc_window* w, const char* title,
				  int band) {
	AdwActionRow* row = ADW_ACTION_ROW(adw_action_row_new());
	GtkScale* scale = GTK_SCALE(gtk_scale_new_with_range(
	    GTK_ORIENTATION_HORIZONTAL, GAIA_EQ_MIN_MB / 10.0,
	    GAIA_EQ_MAX_MB / 10.0, 0.5));

	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
	gtk_scale_set_digits(scale, 1);
	gtk_scale_set_value_pos(scale, GTK_POS_RIGHT);
	gtk_widget_set_size_request(GTK_WIDGET(scale), 180, -1);
	gtk_widget_set_valign(GTK_WIDGET(scale), GTK_ALIGN_CENTER);
	g_object_set_data(G_OBJECT(scale), "band", GINT_TO_POINTER(band));
	g_signal_connect(scale, "value-changed", G_CALLBACK(on_eq_changed), w);
	adw_action_row_add_suffix(row, GTK_WIDGET(scale));

	w->eq[band] = scale;
	return row;
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

	/* equalizer */
	static const char* bands[GAIA_EQ_BANDS] = {"50 Hz", "250 Hz", "800 Hz",
						  "3 kHz", "8 kHz"};
	AdwPreferencesGroup* eq_group =
	    ADW_PREFERENCES_GROUP(adw_preferences_group_new());
	adw_preferences_group_set_title(eq_group, "Equalizer");

	w->bass = ADW_SWITCH_ROW(adw_switch_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(w->bass),
				      "Bass boost");
	g_signal_connect(w->bass, "notify::active", G_CALLBACK(on_bass_toggled),
			 w);
	adw_preferences_group_add(eq_group, GTK_WIDGET(w->bass));
	for (int i = 0; i < GAIA_EQ_BANDS; i++) {
		adw_preferences_group_add(eq_group,
					  GTK_WIDGET(build_eq_row(w, bands[i], i)));
	}
	adw_preferences_page_add(page, eq_group);

	/* device */
	AdwPreferencesGroup* dev_group =
	    ADW_PREFERENCES_GROUP(adw_preferences_group_new());
	AdwActionRow* firmware_row = ADW_ACTION_ROW(adw_action_row_new());

	adw_preferences_group_set_title(dev_group, "Device");
	w->firmware = GTK_LABEL(gtk_label_new("—"));
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(firmware_row),
				      "Firmware");
	adw_action_row_add_suffix(firmware_row, GTK_WIDGET(w->firmware));
	adw_preferences_group_add(dev_group, GTK_WIDGET(firmware_row));
	adw_preferences_page_add(page, dev_group);

	set_rows(w, false);
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

	if (w->mac == NULL) {
		adw_window_title_set_subtitle(w->title, "Looking for the headset…");
	}
	/* NULL mac means: find the headset ourselves, and keep looking */
	start_session(w);

	g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), w);
	return window;
}
