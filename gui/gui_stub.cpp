#include "arco/gui.hpp"

#include <stdexcept>

namespace arco::gui {
namespace { [[noreturn]] void unsupported() { throw std::runtime_error("GUI support was not enabled in this build"); } }
bool available() { return false; }
std::string backend() { return "none"; }
void set_application(const std::string&, const std::string&, const std::string&) { unsupported(); }
int create_window(const std::string&, int, int) { unsupported(); }
void destroy_window(int) { unsupported(); }
bool should_close(int) { unsupported(); }
void set_should_close(int, bool) { unsupported(); }
void set_title(int, const std::string&) { unsupported(); }
Value window_size(int) { unsupported(); }
void clear(int, double, double, double, double) { unsupported(); }
void pixel(int, int, int, double, double, double, double) { unsupported(); }
void fill_rect(int, double, double, double, double, double, double, double, double) { unsupported(); }
void column(int, int, int, int, double, double, double, double) { unsupported(); }
void rectangle(int, double, double, double, double, double, double, double, double) { unsupported(); }
void rounded_rectangle(int, double, double, double, double, double, double, double, double, double) { unsupported(); }
void line(int, double, double, double, double, double, double, double, double, double) { unsupported(); }
void circle(int, double, double, double, double, double, double, double) { unsupported(); }
void text(int, const std::string&, double, double, double, double, double, double, double) { unsupported(); }
void image(int, const std::string&, double, double, double, double, double) { unsupported(); }
Value measure_text(int, const std::string&, double) { unsupported(); }
void set_clip(int, double, double, double, double) { unsupported(); }
void reset_clip(int) { unsupported(); }
std::string clipboard_text(int) { unsupported(); }
void set_clipboard_text(int, const std::string&) { unsupported(); }
void set_cursor(int, const std::string&) { unsupported(); }
bool key_down(int, const std::string&) { unsupported(); }
Value pointer_position(int) { unsupported(); }
std::string open_file_dialog(int, const std::string&, const std::string&) { unsupported(); }
std::string save_file_dialog(int, const std::string&, const std::string&) { unsupported(); }
bool confirm(int, const std::string&, const std::string&) { unsupported(); }
void present(int) { unsupported(); }
Value poll_event() { unsupported(); }
Value wait_event(double) { unsupported(); }
} // namespace arco::gui
