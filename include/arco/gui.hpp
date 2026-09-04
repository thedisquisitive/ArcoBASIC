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
// Scales everything drawn after this call (and until the next GUI.Clear, which always resets to
// unscaled first) by `factor` -- lets a program shrink its whole layout uniformly to fit a
// smaller window (a narrow phone screen, for instance) without touching every individual drawing
// coordinate. Coordinates read back from input (GUI.WaitEvent's event.X/Y, GUI.PointerPosition)
// are NOT auto-adjusted -- a caller using a non-1.0 scale divides those by the same factor itself.
void set_scale(int id, double factor);
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
// Same as text()/measure_text() but rendered in the platform's monospace family instead of the
// proportional default, for code editors and anything else that needs character columns to line
// up (tab stops, a text cursor positioned under a specific character, aligned tabular output).
void text_mono(int id, const std::string& value, double x, double y, double size,
               double red, double green, double blue, double alpha);
void image(int id, const std::string& path, double x, double y, double width, double height, double opacity);
Value measure_text(int id, const std::string& value, double size);
Value measure_text_mono(int id, const std::string& value, double size);
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

// Real, depth-tested 3D rendering (RFC-0047/arco3d's own viewport phase) -- a second layer under
// the ordinary 2D canvas above, not a replacement for it. clear_3d() clears BOTH the real GL
// color+depth buffers (the 3D scene) AND the 2D cairo canvas to fully transparent (so ordinary
// GUI.Text/GUI.Line/etc. calls made afterward, before Present(), composite as a HUD overlay on
// top of the 3D content instead of hiding it); triangle_3d() draws one world-space triangle,
// depth-tested against whatever was already drawn this frame, flat-shaded (a single color for the
// whole triangle -- the caller supplies it, e.g. from a face normal dot a light direction; there
// is no built-in lighting model). Camera is a plain look-at (eye/target/up) plus vertical
// field-of-view in degrees; near/far are the standard perspective clip planes. Present(id) is
// unchanged from the caller's side -- it detects internally whether clear_3d() was used this frame
// and blends the 2D layer over the 3D one instead of replacing it.
void clear_3d(int id, double eye_x, double eye_y, double eye_z, double target_x, double target_y, double target_z,
              double up_x, double up_y, double up_z, double fov_y_degrees, double near_plane, double far_plane,
              double background_red, double background_green, double background_blue);
void triangle_3d(int id, double x1, double y1, double z1, double x2, double y2, double z2, double x3, double y3, double z3,
                 double red, double green, double blue, double alpha);

} // namespace arco::gui
