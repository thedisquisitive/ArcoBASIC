#pragma once

#include <gtk/gtk.h>

#include <string>

namespace lazarus::gui {

void add_class(GtkWidget* widget, const char* css_class);
void install_theme(const std::string& accent = "#f39a22",
                   const std::string& background = "#10161b",
                   const std::string& surface = "#171f25",
                   const std::string& text = "#edf1f3",
                   const std::string& icon_color = "#f39a22");

GtkWidget* make_label(const std::string& text, const char* css_class = nullptr);
GtkWidget* make_icon(const std::string& name, int size = 36);
GtkWidget* make_logo(int size = 96, const std::string& custom_path = {});
void set_logo_path(GtkWidget* image, const std::string& custom_path);
GtkWidget* make_badge(const std::string& text, const std::string& role);
GtkWidget* make_status_card(const std::string& key, const std::string& value);
GtkWidget* make_action_card(const std::string& title, const std::string& detail, const std::string& icon_name, bool primary = false);
GtkWidget* make_section(const std::string& title, GtkWidget* child);

}  // namespace lazarus::gui
