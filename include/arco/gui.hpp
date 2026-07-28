#pragma once

#include "arco/value.hpp"

#include <string>

namespace arco::gui {

bool available();
std::string backend();
void set_application(const std::string& app_id, const std::string& display_name, const std::string& icon_path);
int create_window(const std::string& title, int width, int height);
void destroy_window(int id);
bool should_close(int id);
void set_should_close(int id, bool should_close);
void set_title(int id, const std::string& title);
Value window_size(int id);
void clear(int id, double red, double green, double blue, double alpha);
void pixel(int id, int x, int y, double red, double green, double blue, double alpha);
void fill_rect(int id, double x, double y, double width, double height,
               double red, double green, double blue, double alpha);
void column(int id, int x, int y1, int y2, double red, double green, double blue, double alpha);
void rectangle(int id, double x, double y, double width, double height,
               double red, double green, double blue, double alpha);
void rounded_rectangle(int id, double x, double y, double width, double height, double radius,
                       double red, double green, double blue, double alpha);
void line(int id, double x1, double y1, double x2, double y2, double thickness,
          double red, double green, double blue, double alpha);
void circle(int id, double center_x, double center_y, double radius,
            double red, double green, double blue, double alpha);
void text(int id, const std::string& value, double x, double y, double size,
          double red, double green, double blue, double alpha);
void image(int id, const std::string& path, double x, double y, double width, double height, double opacity);
Value measure_text(int id, const std::string& value, double size);
void set_clip(int id, double x, double y, double width, double height);
void reset_clip(int id);
std::string clipboard_text(int id);
void set_clipboard_text(int id, const std::string& text);
void set_cursor(int id, const std::string& cursor);
bool key_down(int id, const std::string& key);
Value pointer_position(int id);
std::string open_file_dialog(int id, const std::string& title, const std::string& initial_path);
std::string save_file_dialog(int id, const std::string& title, const std::string& initial_path);
bool confirm(int id, const std::string& title, const std::string& message);
void present(int id);
Value poll_event();
Value wait_event(double timeout_seconds);

} // namespace arco::gui
