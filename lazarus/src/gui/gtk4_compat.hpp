#pragma once

// Narrow GTK3-to-GTK4 bridge used while Lazarus moves its existing C UI to
// GTK4. New code should use GTK4 APIs directly; this only keeps the migration
// reviewable instead of mixing layout and ownership changes everywhere.
#include <gtk/gtk.h>

inline void lazarus_set_margins(GtkWidget* widget, guint value) {
    gtk_widget_set_margin_start(widget, value);
    gtk_widget_set_margin_end(widget, value);
    gtk_widget_set_margin_top(widget, value);
    gtk_widget_set_margin_bottom(widget, value);
}

inline void lazarus_container_add(GtkWidget* parent, GtkWidget* child) {
    if (GTK_IS_WINDOW(parent)) {
        gtk_window_set_child(GTK_WINDOW(parent), child);
    } else if (GTK_IS_FRAME(parent)) {
        gtk_frame_set_child(GTK_FRAME(parent), child);
    } else if (GTK_IS_SCROLLED_WINDOW(parent)) {
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(parent), child);
    } else if (GTK_IS_BUTTON(parent)) {
        gtk_button_set_child(GTK_BUTTON(parent), child);
    } else if (GTK_IS_LIST_BOX_ROW(parent)) {
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(parent), child);
    } else if (GTK_IS_POPOVER(parent)) {
        gtk_popover_set_child(GTK_POPOVER(parent), child);
    }
}

inline GList* lazarus_container_children(GtkWidget* parent) {
    GList* result = nullptr;
    for (GtkWidget* child = gtk_widget_get_first_child(parent); child != nullptr;
         child = gtk_widget_get_next_sibling(child)) {
        result = g_list_append(result, child);
    }
    return result;
}

inline void lazarus_destroy_widget(GtkWidget* widget) {
    if (GTK_IS_WINDOW(widget)) {
        gtk_window_destroy(GTK_WINDOW(widget));
        return;
    }
    if (GtkWidget* parent = gtk_widget_get_parent(widget); GTK_IS_LIST_BOX(parent)) {
        gtk_list_box_remove(GTK_LIST_BOX(parent), widget);
    } else {
        gtk_widget_unparent(widget);
    }
}

inline void lazarus_show_all(GtkWidget* widget) {
    for (GtkWidget* child = gtk_widget_get_first_child(widget); child != nullptr;
         child = gtk_widget_get_next_sibling(child)) {
        lazarus_show_all(child);
    }
    gtk_widget_show(widget);
}

inline void lazarus_box_pack_start(GtkBox* box, GtkWidget* child, gboolean, gboolean expand, guint) {
    gtk_widget_set_hexpand(child, expand);
    gtk_box_append(box, child);
}

inline void lazarus_box_pack_end(GtkBox* box, GtkWidget* child, gboolean, gboolean expand, guint) {
    gtk_widget_set_hexpand(child, expand);
    gtk_box_prepend(box, child);
}

inline GtkWidget* lazarus_scrolled_window_new() { return gtk_scrolled_window_new(); }
inline void lazarus_paned_pack1(GtkPaned* paned, GtkWidget* child, gboolean resize, gboolean) {
    gtk_widget_set_hexpand(child, resize);
    gtk_paned_set_start_child(paned, child);
}
inline void lazarus_paned_pack2(GtkPaned* paned, GtkWidget* child, gboolean resize, gboolean) {
    gtk_widget_set_hexpand(child, resize);
    gtk_paned_set_end_child(paned, child);
}

inline void lazarus_pump_events() {
    while (g_main_context_pending(nullptr)) {
        g_main_context_iteration(nullptr, FALSE);
    }
}

inline gint lazarus_dialog_run(GtkDialog* dialog) {
    struct RunState { GMainLoop* loop; gint response = GTK_RESPONSE_CANCEL; } state{g_main_loop_new(nullptr, FALSE)};
    const gulong response_handler = g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog*, gint response, gpointer data) {
        auto* state = static_cast<RunState*>(data);
        state->response = response;
        g_main_loop_quit(state->loop);
    }), &state);
    const gulong close_handler = g_signal_connect(dialog, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer data) -> gboolean {
        auto* state = static_cast<RunState*>(data);
        g_main_loop_quit(state->loop);
        return FALSE;
    }), &state);
    gtk_window_present(GTK_WINDOW(dialog));
    g_main_loop_run(state.loop);
    g_signal_handler_disconnect(dialog, response_handler);
    g_signal_handler_disconnect(dialog, close_handler);
    g_main_loop_unref(state.loop);
    return state.response;
}

inline char* lazarus_file_chooser_get_filename(GtkFileChooser* chooser) {
    GFile* file = gtk_file_chooser_get_file(chooser);
    if (file == nullptr) {
        return nullptr;
    }
    char* path = g_file_get_path(file);
    g_object_unref(file);
    return path;
}

#define GTK_CONTAINER(widget) (GTK_WIDGET(widget))
#define gtk_container_set_border_width(widget, value) lazarus_set_margins(GTK_WIDGET(widget), value)
#define gtk_container_add(parent, child) lazarus_container_add(GTK_WIDGET(parent), GTK_WIDGET(child))
#define gtk_container_get_children(parent) lazarus_container_children(GTK_WIDGET(parent))
#define gtk_widget_destroy(widget) lazarus_destroy_widget(GTK_WIDGET(widget))
#define gtk_widget_show_all(widget) lazarus_show_all(GTK_WIDGET(widget))
#define gtk_box_pack_start(box, child, expand, fill, padding) lazarus_box_pack_start(GTK_BOX(box), GTK_WIDGET(child), expand, fill, padding)
#define gtk_box_pack_end(box, child, expand, fill, padding) lazarus_box_pack_end(GTK_BOX(box), GTK_WIDGET(child), expand, fill, padding)
#define gtk_scrolled_window_new(...) lazarus_scrolled_window_new()
#define gtk_paned_pack1(paned, child, resize, shrink) lazarus_paned_pack1(GTK_PANED(paned), GTK_WIDGET(child), resize, shrink)
#define gtk_paned_pack2(paned, child, resize, shrink) lazarus_paned_pack2(GTK_PANED(paned), GTK_WIDGET(child), resize, shrink)
#define gtk_window_resize(window, width, height) gtk_window_set_default_size(GTK_WINDOW(window), width, height)
#define gtk_entry_get_text(entry) gtk_editable_get_text(GTK_EDITABLE(entry))
#define gtk_entry_set_text(entry, text) gtk_editable_set_text(GTK_EDITABLE(entry), text)
#define gtk_dialog_run(dialog) lazarus_dialog_run(GTK_DIALOG(dialog))
#define gtk_file_chooser_get_filename(chooser) lazarus_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser))
#define GTK_WIN_POS_CENTER_ON_PARENT 0
#define gtk_window_set_position(window, position) ((void)0)
#define gtk_window_move(window, x, y) ((void)0)
