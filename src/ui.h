#ifndef UI_H
#define UI_H

/* Forward declarations keep this header free of the GTK include path. */
typedef struct _GtkApplication GtkApplication;
typedef struct _GtkWidget GtkWidget;

/* Builds the main window and connects it to mac (may be NULL). */
GtkWidget* mc_window_new(GtkApplication* app, const char* mac);

#endif // UI_H
