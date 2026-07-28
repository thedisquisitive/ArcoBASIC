#include "arco/gui.hpp"

#include <GLFW/glfw3.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace arco::gui {
namespace {

struct WindowRecord {
    int id = 0;
    GLFWwindow* handle = nullptr;
    int canvas_width = 0;
    int canvas_height = 0;
    std::vector<unsigned char> pixels;
    cairo_surface_t* surface = nullptr;
    cairo_t* context = nullptr;
    GLuint texture = 0;
    bool clip_active = false;
    double last_click_time = -1;
    double last_click_x = 0;
    double last_click_y = 0;
    int last_click_button = -1;

    ~WindowRecord() {
        if (context) cairo_destroy(context);
        if (surface) cairo_surface_destroy(surface);
    }
};

bool initialized = false;
bool dialogs_initialized = false;
int next_id = 1;
std::unordered_map<int, std::unique_ptr<WindowRecord>> windows;
std::deque<Value> events;
std::unordered_map<std::string, cairo_surface_t*> image_cache;
GLFWcursor* arrow_cursor = nullptr;
GLFWcursor* text_cursor = nullptr;
GLFWcursor* hand_cursor = nullptr;
std::string application_id = "arcobasic";
std::string application_name = "ArcoBASIC";
std::string application_icon_path;

void ensure_initialized() {
    if (initialized) return;
#if GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4
    glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
#endif
    if (!glfwInit()) throw std::runtime_error("GUI initialization failed: no usable Wayland or X11 display");
    initialized = true;
}

void ensure_dialogs_initialized() {
    if (dialogs_initialized) return;
    if (!gtk_init_check(nullptr, nullptr)) throw std::runtime_error("native Linux dialogs are unavailable in this desktop session");
    dialogs_initialized = true;
}

void set_initial_path(GtkFileChooser* chooser, const std::string& path, bool save) {
    if (path.empty()) return;
    if (save) {
        gchar* directory = g_path_get_dirname(path.c_str());
        gchar* name = g_path_get_basename(path.c_str());
        gtk_file_chooser_set_current_folder(chooser, directory);
        gtk_file_chooser_set_current_name(chooser, name);
        g_free(directory);
        g_free(name);
    } else {
        gtk_file_chooser_set_filename(chooser, path.c_str());
    }
}

WindowRecord& find_window(int id) {
    const auto found = windows.find(id);
    if (found == windows.end()) throw std::runtime_error("unknown GUI window: " + std::to_string(id));
    return *found->second;
}

WindowRecord* record(GLFWwindow* handle) {
    return static_cast<WindowRecord*>(glfwGetWindowUserPointer(handle));
}

void resize_canvas(WindowRecord& item, int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (item.canvas_width == width && item.canvas_height == height) return;
    if (item.context) cairo_destroy(item.context);
    if (item.surface) cairo_surface_destroy(item.surface);
    item.canvas_width = width;
    item.canvas_height = height;
    item.pixels.assign(static_cast<std::size_t>(width * height * 4), 0);
    item.surface = cairo_image_surface_create_for_data(item.pixels.data(), CAIRO_FORMAT_ARGB32, width, height, width * 4);
    item.context = cairo_create(item.surface);
    item.clip_active = false;
}

void set_color(cairo_t* context, double red, double green, double blue, double alpha) {
    cairo_set_source_rgba(context, std::clamp(red, 0.0, 1.0), std::clamp(green, 0.0, 1.0),
                          std::clamp(blue, 0.0, 1.0), std::clamp(alpha, 0.0, 1.0));
}

void rounded_path(cairo_t* context, double x, double y, double width, double height, double radius) {
    constexpr double pi = 3.14159265358979323846;
    radius = std::max(0.0, std::min(radius, std::min(width, height) / 2.0));
    cairo_new_sub_path(context);
    cairo_arc(context, x + width - radius, y + radius, radius, -pi / 2, 0);
    cairo_arc(context, x + width - radius, y + height - radius, radius, 0, pi / 2);
    cairo_arc(context, x + radius, y + height - radius, radius, pi / 2, pi);
    cairo_arc(context, x + radius, y + radius, radius, pi, pi * 3 / 2);
    cairo_close_path(context);
}

void push_pointer(GLFWwindow* handle, const char* type, double x, double y) {
    const auto* item = record(handle);
    if (!item) return;
    events.emplace_back(Value::Object{{"Type", type}, {"Window", item->id}, {"X", x}, {"Y", y}});
}

std::string key_name(int key) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return std::string(1, static_cast<char>('a' + key - GLFW_KEY_A));
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) return std::string(1, static_cast<char>('0' + key - GLFW_KEY_0));
    switch (key) {
        case GLFW_KEY_ESCAPE: return "escape";
        case GLFW_KEY_ENTER: return "enter";
        case GLFW_KEY_SPACE: return "space";
        case GLFW_KEY_LEFT: return "left";
        case GLFW_KEY_RIGHT: return "right";
        case GLFW_KEY_UP: return "up";
        case GLFW_KEY_DOWN: return "down";
        case GLFW_KEY_HOME: return "home";
        case GLFW_KEY_END: return "end";
        case GLFW_KEY_PAGE_UP: return "pageup";
        case GLFW_KEY_PAGE_DOWN: return "pagedown";
        case GLFW_KEY_TAB: return "tab";
        case GLFW_KEY_BACKSPACE: return "backspace";
        case GLFW_KEY_DELETE: return "delete";
        default: return "unknown";
    }
}

int key_code(const std::string& key) {
    if (key.size() == 1) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(key[0])));
        if (c >= 'a' && c <= 'z') return GLFW_KEY_A + (c - 'a');
        if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
    }
    if (key == "escape") return GLFW_KEY_ESCAPE;
    if (key == "enter") return GLFW_KEY_ENTER;
    if (key == "space") return GLFW_KEY_SPACE;
    if (key == "left") return GLFW_KEY_LEFT;
    if (key == "right") return GLFW_KEY_RIGHT;
    if (key == "up") return GLFW_KEY_UP;
    if (key == "down") return GLFW_KEY_DOWN;
    if (key == "home") return GLFW_KEY_HOME;
    if (key == "end") return GLFW_KEY_END;
    if (key == "pageup" || key == "page-up") return GLFW_KEY_PAGE_UP;
    if (key == "pagedown" || key == "page-down") return GLFW_KEY_PAGE_DOWN;
    if (key == "tab") return GLFW_KEY_TAB;
    if (key == "backspace") return GLFW_KEY_BACKSPACE;
    if (key == "delete") return GLFW_KEY_DELETE;
    if (key == "shift") return GLFW_KEY_LEFT_SHIFT;
    if (key == "ctrl" || key == "control") return GLFW_KEY_LEFT_CONTROL;
    if (key == "alt") return GLFW_KEY_LEFT_ALT;
    if (key == "f1") return GLFW_KEY_F1;
    if (key == "f2") return GLFW_KEY_F2;
    if (key == "f3") return GLFW_KEY_F3;
    if (key == "f4") return GLFW_KEY_F4;
    if (key == "f5") return GLFW_KEY_F5;
    if (key == "f6") return GLFW_KEY_F6;
    if (key == "f7") return GLFW_KEY_F7;
    if (key == "f8") return GLFW_KEY_F8;
    if (key == "f9") return GLFW_KEY_F9;
    if (key == "f10") return GLFW_KEY_F10;
    if (key == "f11") return GLFW_KEY_F11;
    if (key == "f12") return GLFW_KEY_F12;
    return GLFW_KEY_UNKNOWN;
}

std::string utf8(unsigned int codepoint) {
    std::string value;
    if (codepoint <= 0x7f) value.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7ff) {
        value.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        value.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        value.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        value.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        value.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0x10ffff) {
        value.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        value.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        value.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        value.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
    return value;
}

Value::Object modifiers(int mods) {
    return {{"Shift", (mods & GLFW_MOD_SHIFT) != 0}, {"Ctrl", (mods & GLFW_MOD_CONTROL) != 0},
            {"Alt", (mods & GLFW_MOD_ALT) != 0}, {"Super", (mods & GLFW_MOD_SUPER) != 0}};
}

Value no_event() { return Value::Object{{"Type", "none"}}; }

unsigned char color_byte(double value) {
    return static_cast<unsigned char>(std::clamp(value, 0.0, 1.0) * 255.0 + 0.5);
}

void write_pixel(WindowRecord& item, int x, int y, double r, double g, double b, double a) {
    if (x < 0 || y < 0 || x >= item.canvas_width || y >= item.canvas_height) return;
    const unsigned char alpha = color_byte(a);
    const auto premultiply = [&](double channel) {
        return static_cast<unsigned char>((static_cast<int>(color_byte(channel)) * static_cast<int>(alpha) + 127) / 255);
    };
    unsigned char* dst = item.pixels.data() + static_cast<std::size_t>((y * item.canvas_width + x) * 4);
    dst[0] = premultiply(b);
    dst[1] = premultiply(g);
    dst[2] = premultiply(r);
    dst[3] = alpha;
}

void apply_application_hints() {
#ifdef GLFW_WAYLAND_APP_ID
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, application_id.c_str());
#endif
#ifdef GLFW_X11_CLASS_NAME
    glfwWindowHintString(GLFW_X11_CLASS_NAME, application_id.c_str());
#endif
#ifdef GLFW_X11_INSTANCE_NAME
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, application_id.c_str());
#endif
}

void apply_window_icon(GLFWwindow* handle) {
    if (application_icon_path.empty()) return;
    GError* error = nullptr;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file(application_icon_path.c_str(), &error);
    if (!pixbuf) {
        std::string message = "could not load GUI application icon " + application_icon_path;
        if (error) {
            message += ": ";
            message += error->message;
            g_error_free(error);
        }
        throw std::runtime_error(message);
    }

    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    const int channels = gdk_pixbuf_get_n_channels(pixbuf);
    const bool has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
    const guchar* source = gdk_pixbuf_get_pixels(pixbuf);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4), 255);
    for (int y = 0; y < height; ++y) {
        const guchar* row = source + y * rowstride;
        for (int x = 0; x < width; ++x) {
            const guchar* src = row + x * channels;
            unsigned char* dst = pixels.data() + static_cast<std::size_t>((y * width + x) * 4);
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = has_alpha ? src[3] : 255;
        }
    }
    GLFWimage image{width, height, pixels.data()};
    glfwSetWindowIcon(handle, 1, &image);
    g_object_unref(pixbuf);
}

void configure_callbacks(WindowRecord& item) {
    glfwSetWindowUserPointer(item.handle, &item);
    glfwSetWindowCloseCallback(item.handle, [](GLFWwindow* handle) {
        auto* item = record(handle);
        if (item) events.emplace_back(Value::Object{{"Type", "close"}, {"Window", item->id}});
    });
    glfwSetFramebufferSizeCallback(item.handle, [](GLFWwindow* handle, int width, int height) {
        auto* item = record(handle);
        if (item) events.emplace_back(Value::Object{{"Type", "resize"}, {"Window", item->id}, {"Width", width}, {"Height", height}});
    });
    glfwSetWindowSizeCallback(item.handle, [](GLFWwindow* handle, int width, int height) {
        auto* item = record(handle);
        if (item) resize_canvas(*item, width, height);
    });
    glfwSetCursorPosCallback(item.handle, [](GLFWwindow* handle, double x, double y) { push_pointer(handle, "pointer-move", x, y); });
    glfwSetMouseButtonCallback(item.handle, [](GLFWwindow* handle, int button, int action, int mods) {
        auto* item = record(handle);
        if (!item) return;
        double x = 0, y = 0;
        glfwGetCursorPos(handle, &x, &y);
        const char* name = button == GLFW_MOUSE_BUTTON_LEFT ? "left" : (button == GLFW_MOUSE_BUTTON_RIGHT ? "right" : "middle");
        int clicks = 1;
        if (action == GLFW_PRESS) {
            const double now = glfwGetTime();
            if (item->last_click_button == button && now - item->last_click_time <= 0.45 &&
                std::abs(x - item->last_click_x) <= 5 && std::abs(y - item->last_click_y) <= 5) clicks = 2;
            item->last_click_time = now;
            item->last_click_x = x;
            item->last_click_y = y;
            item->last_click_button = button;
        }
        auto event = modifiers(mods);
        event.insert({{"Type", "pointer-button"}, {"Window", item->id}, {"Action", action == GLFW_PRESS ? "press" : "release"},
                      {"Button", name}, {"Clicks", clicks}, {"X", x}, {"Y", y}});
        events.emplace_back(std::move(event));
    });
    glfwSetScrollCallback(item.handle, [](GLFWwindow* handle, double x, double y) {
        auto* item = record(handle);
        if (item) events.emplace_back(Value::Object{{"Type", "scroll"}, {"Window", item->id}, {"DeltaX", x}, {"DeltaY", y}});
    });
    glfwSetKeyCallback(item.handle, [](GLFWwindow* handle, int key, int, int action, int mods) {
        auto* item = record(handle);
        if (!item) return;
        auto event = modifiers(mods);
        event.insert({{"Type", "key"}, {"Window", item->id}, {"Key", key_name(key)},
                      {"Action", action == GLFW_PRESS ? "press" : (action == GLFW_REPEAT ? "repeat" : "release")}});
        events.emplace_back(std::move(event));
    });
    glfwSetCharCallback(item.handle, [](GLFWwindow* handle, unsigned int codepoint) {
        auto* item = record(handle);
        if (item) events.emplace_back(Value::Object{{"Type", "text"}, {"Window", item->id}, {"Text", utf8(codepoint)}});
    });
}

void begin_present(WindowRecord& item) {
    glfwMakeContextCurrent(item.handle);
    int framebuffer_width = 0, framebuffer_height = 0, width = 0, height = 0;
    glfwGetFramebufferSize(item.handle, &framebuffer_width, &framebuffer_height);
    glfwGetWindowSize(item.handle, &width, &height);
    glViewport(0, 0, framebuffer_width, framebuffer_height);
}

} // namespace

bool available() { return true; }
std::string backend() { return "glfw"; }

void set_application(const std::string& app_id, const std::string& display_name, const std::string& icon_path) {
    if (app_id.empty()) throw std::runtime_error("GUI.Application app id must not be empty");
    application_id = app_id;
    application_name = display_name.empty() ? app_id : display_name;
    application_icon_path = icon_path;
    g_set_application_name(application_name.c_str());
}

int create_window(const std::string& title, int width, int height) {
    ensure_initialized();
    if (width <= 0 || height <= 0) throw std::runtime_error("GUI.Window dimensions must be positive");
    apply_application_hints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    GLFWwindow* handle = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!handle) throw std::runtime_error("GUI window creation failed");
    apply_window_icon(handle);
    auto item = std::make_unique<WindowRecord>();
    item->id = next_id++;
    item->handle = handle;
    const int id = item->id;
    configure_callbacks(*item);
    resize_canvas(*item, width, height);
    windows.emplace(id, std::move(item));
    glfwMakeContextCurrent(handle);
    glGenTextures(1, &windows.at(id)->texture);
    glfwSwapInterval(1);
    return id;
}

void destroy_window(int id) {
    auto& item = find_window(id);
    glfwMakeContextCurrent(item.handle);
    if (item.texture) glDeleteTextures(1, &item.texture);
    glfwDestroyWindow(item.handle);
    windows.erase(id);
}

bool should_close(int id) { return glfwWindowShouldClose(find_window(id).handle); }
void set_should_close(int id, bool should_close) { glfwSetWindowShouldClose(find_window(id).handle, should_close ? GLFW_TRUE : GLFW_FALSE); }
void set_title(int id, const std::string& title) { glfwSetWindowTitle(find_window(id).handle, title.c_str()); }
Value window_size(int id) {
    int width = 0, height = 0;
    glfwGetWindowSize(find_window(id).handle, &width, &height);
    return Value::Object{{"Width", width}, {"Height", height}};
}
void clear(int id, double r, double g, double b, double a) {
    auto& item = find_window(id);
    cairo_save(item.context);
    cairo_set_operator(item.context, CAIRO_OPERATOR_SOURCE);
    set_color(item.context, r, g, b, a);
    cairo_paint(item.context);
    cairo_restore(item.context);
}
void pixel(int id, int x, int y, double r, double g, double b, double a) {
    auto& item = find_window(id);
    write_pixel(item, x, y, r, g, b, a);
    cairo_surface_mark_dirty_rectangle(item.surface, x, y, 1, 1);
}
void fill_rect(int id, double x, double y, double width, double height, double r, double g, double b, double a) {
    rectangle(id, x, y, width, height, r, g, b, a);
}
void column(int id, int x, int y1, int y2, double r, double g, double b, double a) {
    auto& item = find_window(id);
    if (x < 0 || x >= item.canvas_width) return;
    if (y1 > y2) std::swap(y1, y2);
    y1 = std::max(0, y1);
    y2 = std::min(item.canvas_height - 1, y2);
    if (y1 > y2) return;
    for (int y = y1; y <= y2; ++y) {
        write_pixel(item, x, y, r, g, b, a);
    }
    cairo_surface_mark_dirty_rectangle(item.surface, x, y1, 1, y2 - y1 + 1);
}
void rectangle(int id, double x, double y, double width, double height, double r, double g, double b, double a) {
    auto& item = find_window(id);
    cairo_rectangle(item.context, x, y, width, height);
    set_color(item.context, r, g, b, a);
    cairo_fill(item.context);
}
void rounded_rectangle(int id, double x, double y, double width, double height, double radius, double r, double g, double b, double a) {
    auto& item = find_window(id);
    rounded_path(item.context, x, y, width, height, radius);
    set_color(item.context, r, g, b, a);
    cairo_fill(item.context);
}
void line(int id, double x1, double y1, double x2, double y2, double thickness, double r, double g, double b, double a) {
    auto& item = find_window(id);
    cairo_move_to(item.context, x1, y1);
    cairo_line_to(item.context, x2, y2);
    cairo_set_line_width(item.context, std::max(0.1, thickness));
    set_color(item.context, r, g, b, a);
    cairo_stroke(item.context);
}
void circle(int id, double center_x, double center_y, double radius, double r, double g, double b, double a) {
    auto& item = find_window(id);
    constexpr double pi = 3.14159265358979323846;
    cairo_arc(item.context, center_x, center_y, std::max(0.0, radius), 0, pi * 2);
    set_color(item.context, r, g, b, a);
    cairo_fill(item.context);
}
void text(int id, const std::string& value, double x, double y, double size, double r, double g, double b, double a) {
    auto& item = find_window(id);
    PangoLayout* layout = pango_cairo_create_layout(item.context);
    PangoFontDescription* font = pango_font_description_new();
    pango_font_description_set_family(font, "Sans");
    pango_font_description_set_absolute_size(font, std::max(1.0, size) * PANGO_SCALE);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, value.c_str(), -1);
    cairo_move_to(item.context, x, y);
    set_color(item.context, r, g, b, a);
    pango_cairo_show_layout(item.context, layout);
    pango_font_description_free(font);
    g_object_unref(layout);
}
void image(int id, const std::string& path, double x, double y, double width, double height, double opacity) {
    auto& item = find_window(id);
    if (width <= 0 || height <= 0) return;
    cairo_surface_t* surface = nullptr;
    const auto cached = image_cache.find(path);
    if (cached != image_cache.end()) {
        surface = cached->second;
    } else {
        surface = cairo_image_surface_create_from_png(path.c_str());
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            const std::string error = cairo_status_to_string(cairo_surface_status(surface));
            cairo_surface_destroy(surface);
            throw std::runtime_error("could not load GUI image " + path + ": " + error);
        }
        image_cache.emplace(path, surface);
    }
    const double source_width = cairo_image_surface_get_width(surface);
    const double source_height = cairo_image_surface_get_height(surface);
    cairo_save(item.context);
    cairo_translate(item.context, x, y);
    cairo_scale(item.context, width / source_width, height / source_height);
    cairo_set_source_surface(item.context, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(item.context), CAIRO_FILTER_BILINEAR);
    cairo_paint_with_alpha(item.context, std::clamp(opacity, 0.0, 1.0));
    cairo_restore(item.context);
}
Value measure_text(int id, const std::string& value, double size) {
    auto& item = find_window(id);
    PangoLayout* layout = pango_cairo_create_layout(item.context);
    PangoFontDescription* font = pango_font_description_new();
    pango_font_description_set_family(font, "Sans");
    pango_font_description_set_absolute_size(font, std::max(1.0, size) * PANGO_SCALE);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, value.c_str(), -1);
    int width = 0, height = 0;
    pango_layout_get_pixel_size(layout, &width, &height);
    pango_font_description_free(font);
    g_object_unref(layout);
    return Value::Object{{"Width", width}, {"Height", height}};
}
void set_clip(int id, double x, double y, double width, double height) {
    auto& item = find_window(id);
    if (item.clip_active) cairo_restore(item.context);
    cairo_save(item.context);
    cairo_rectangle(item.context, x, y, width, height);
    cairo_clip(item.context);
    item.clip_active = true;
}
void reset_clip(int id) {
    auto& item = find_window(id);
    if (item.clip_active) {
        cairo_restore(item.context);
        item.clip_active = false;
    }
}
std::string clipboard_text(int id) {
    const char* value = glfwGetClipboardString(find_window(id).handle);
    return value ? value : "";
}
void set_clipboard_text(int id, const std::string& value) { glfwSetClipboardString(find_window(id).handle, value.c_str()); }
void set_cursor(int id, const std::string& cursor) {
    auto& item = find_window(id);
    GLFWcursor** selected = &arrow_cursor;
    int shape = GLFW_ARROW_CURSOR;
    if (cursor == "text") { selected = &text_cursor; shape = GLFW_IBEAM_CURSOR; }
    else if (cursor == "hand") { selected = &hand_cursor; shape = GLFW_HAND_CURSOR; }
    else if (cursor != "default" && cursor != "arrow") throw std::runtime_error("unknown GUI cursor: " + cursor);
    if (!*selected) *selected = glfwCreateStandardCursor(shape);
    glfwSetCursor(item.handle, *selected);
}
bool key_down(int id, const std::string& key) {
    auto& item = find_window(id);
    glfwPollEvents();
    const int code = key_code(key);
    if (code == GLFW_KEY_UNKNOWN) return false;
    const int state = glfwGetKey(item.handle, code);
    return state == GLFW_PRESS || state == GLFW_REPEAT;
}
Value pointer_position(int id) {
    auto& item = find_window(id);
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(item.handle, &x, &y);
    return Value::Object{{"X", x}, {"Y", y}};
}
std::string file_dialog(const std::string& title, const std::string& initial_path, bool save) {
    ensure_dialogs_initialized();
    GtkFileChooserNative* dialog = gtk_file_chooser_native_new(title.c_str(), nullptr,
        save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
        save ? "Save" : "Open", "Cancel");
    GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);
    if (save) gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    set_initial_path(chooser, initial_path, save);
    std::string result;
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(chooser);
        if (filename) { result = filename; g_free(filename); }
    }
    g_object_unref(dialog);
    return result;
}
std::string open_file_dialog(int id, const std::string& title, const std::string& initial_path) {
    (void)find_window(id);
    return file_dialog(title, initial_path, false);
}
std::string save_file_dialog(int id, const std::string& title, const std::string& initial_path) {
    (void)find_window(id);
    return file_dialog(title, initial_path, true);
}
bool confirm(int id, const std::string& title, const std::string& message) {
    (void)find_window(id);
    ensure_dialogs_initialized();
    GtkWidget* dialog = gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", message.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL, "Continue", GTK_RESPONSE_ACCEPT, nullptr);
    const bool accepted = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT;
    gtk_widget_destroy(dialog);
    while (gtk_events_pending()) gtk_main_iteration();
    return accepted;
}
void present(int id) {
    auto& item = find_window(id);
    begin_present(item);
    cairo_surface_flush(item.surface);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, item.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, item.canvas_width, item.canvas_height, 0, GL_BGRA, GL_UNSIGNED_BYTE, item.pixels.data());
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1, 1, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glColor4d(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2d(0, 0); glVertex2d(0, 0); glTexCoord2d(1, 0); glVertex2d(1, 0);
    glTexCoord2d(1, 1); glVertex2d(1, 1); glTexCoord2d(0, 1); glVertex2d(0, 1);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glfwSwapBuffers(item.handle);
}
Value poll_event() {
    ensure_initialized();
    glfwPollEvents();
    if (events.empty()) return no_event();
    Value event = events.front(); events.pop_front(); return event;
}
Value wait_event(double timeout) {
    ensure_initialized();
    if (events.empty()) glfwWaitEventsTimeout(std::max(0.0, timeout));
    if (events.empty()) return no_event();
    Value event = events.front(); events.pop_front(); return event;
}

} // namespace arco::gui
