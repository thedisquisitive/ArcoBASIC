// Browser GUI backend for the web capsule target (see ArcoFission's `web` build, fission.cpp),
// implementing the same arco::gui interface src/gui/glfw_backend.cpp does for desktop, backed by
// an HTML5 <canvas> + its 2D rendering context instead of GLFW+Cairo+Pango+GTK -- none of which
// exist in a browser. Every window is a real DOM <canvas> element that tracks the full browser
// viewport (there's no concept of a resizable desktop window to request a *specific* size from in
// a page; create_window's width/height are only the initial value window_size() reports before
// the first real layout, same as create_window's title argument only sets document.title).
//
// The blocking-style main loop stdlib/gui.abas's widgets and arcoflow/arcoflow.abas are written
// against (`WHILE app.Running ... event = GUI.WaitEvent(...) ... WEND`) is preserved unchanged --
// wait_event() below yields to the browser's own event loop via emscripten_sleep() while polling,
// which only works because the web capsule is linked with `-sASYNCIFY` (see fission.cpp): that
// flag lets the *entire* C++ call stack, from main() down through the bytecode VM down to this
// function, transparently pause and resume around that blocking-looking call instead of needing
// the whole runtime rewritten into a callback/state-machine style. This is Emscripten's documented
// way to port existing blocking game/GUI-loop C++ code to the web with no structural rewrite.
//
// Known, deliberate gaps for this first pass (each throws a clear error rather than silently
// misbehaving): GUI.Image (loading an image file is inherently asynchronous in a browser; wiring
// that through Asyncify + fetch is a real follow-up, not done here). File.* still works against
// Emscripten's default in-memory filesystem (MEMFS) with no code changes needed on that side, but
// it's ephemeral -- gone on page reload, invisible to the user's real disk -- so
// open_file_dialog/save_file_dialog use a plain window.prompt() for a path into that ephemeral FS
// rather than a real native picker (no synchronous browser API for one; a proper implementation
// would bridge the File System Access API through Asyncify, another real follow-up). Clipboard
// access is similarly a no-op stub (real clipboard access is async/permission-gated in browsers).

#include "arco/gui.hpp"

#include <emscripten.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace arco::gui {
namespace {

struct WindowRecord {
    int id = 0;
    std::string title;
    int width = 0;
    int height = 0;
};

bool initialized = false;
int next_id = 1;
std::unordered_map<int, WindowRecord> windows;
std::deque<Value> events;
std::unordered_set<std::string> keys_down;

WindowRecord& find_window(int id) {
    const auto found = windows.find(id);
    if (found == windows.end()) throw std::runtime_error("unknown GUI window: " + std::to_string(id));
    return found->second;
}

Value no_event() { return Value::Object{{"Type", "none"}}; }

// One-time DOM/JS-side setup: a Module.arcoGui namespace holding per-window <canvas> elements
// and 2D contexts, plus the key-name normalizer shared by every keyboard listener (JS
// KeyboardEvent.key values aren't the same strings ArcoBASIC uses -- glfw_backend.cpp's key_name()
// does the equivalent normalization for desktop's numeric GLFW key codes).
EM_JS(void, js_ensure_initialized, (), {
    if (Module.arcoGui) return;
    Module.arcoGui = {canvases: {}, ctxs: {}};
    Module.arcoKeyName = function(e) {
        const key = e.key;
        if (key.length === 1) return key.toLowerCase();
        const named = {
            Escape: "escape", Enter: "enter", " ": "space", Spacebar: "space",
            ArrowLeft: "left", ArrowRight: "right", ArrowUp: "up", ArrowDown: "down",
            Home: "home", End: "end", PageUp: "pageup", PageDown: "pagedown",
            Tab: "tab", Backspace: "backspace", Delete: "delete",
        };
        if (named[key]) return named[key];
        if (/^F([1-9]|1[0-9]|2[0-5])$/.test(key)) return key.toLowerCase();
        return "unknown";
    };
    // DOM event listeners fire whenever the browser feels like it -- including while the C++ call
    // stack is unwound mid-emscripten_sleep() inside wait_event() below (see -sASYNCIFY). Calling
    // straight back into the wasm module from a listener in that window is a real, documented
    // Asyncify limitation (reentrant calls while an async operation is in flight corrupt its
    // state), and does: it silently throws inside the next resumed C++ frame the moment a key is
    // pressed. So listeners only ever push a plain JS object here; the actual
    // Module._arco_gui_push_*/ccall calls happen later, in arcoFlushPendingEvents, called only
    // from js_flush_pending_events() -- a normal, non-reentrant call site the C++ side reaches
    // between sleeps, never from inside a listener.
    Module.arcoGui.pendingEvents = [];
    Module.arcoQueueEvent = function(evt) { Module.arcoGui.pendingEvents.push(evt); };
    Module.arcoFlushPendingEvents = function() {
        const list = Module.arcoGui.pendingEvents;
        Module.arcoGui.pendingEvents = [];
        for (const evt of list) {
            switch (evt.type) {
                case "pointer-move":
                    Module._arco_gui_push_pointer_move(evt.id, evt.x, evt.y);
                    break;
                case "pointer-button":
                    Module._arco_gui_push_pointer_button(evt.id, evt.x, evt.y, evt.pressed, evt.button,
                        evt.shift, evt.ctrl, evt.alt, evt.meta);
                    break;
                case "key":
                    Module.ccall("arco_gui_push_key", null,
                        ["number", "string", "string", "number", "number", "number", "number"],
                        [evt.id, evt.key, evt.action, evt.shift, evt.ctrl, evt.alt, evt.meta]);
                    break;
                case "text":
                    Module.ccall("arco_gui_push_text", null, ["number", "string"], [evt.id, evt.text]);
                    break;
                case "scroll":
                    Module._arco_gui_push_scroll(evt.id, evt.dx, evt.dy, evt.x, evt.y);
                    break;
                case "resize":
                    Module._arco_gui_push_resize(evt.id, evt.width, evt.height);
                    break;
            }
        }
    };
});

EM_JS(void, js_flush_pending_events, (), { Module.arcoFlushPendingEvents(); });

EM_JS(void, js_create_window, (int id, const char* title_ptr, int width, int height), {
    const title = UTF8ToString(title_ptr);
    document.title = title;
    document.body.style.margin = "0";
    document.body.style.overflow = "hidden";
    document.body.style.background = "#000";
    const canvas = document.createElement("canvas");
    canvas.id = "arco-gui-canvas-" + id;
    canvas.width = width;
    canvas.height = height;
    canvas.tabIndex = 0;
    canvas.style.display = "block";
    canvas.style.outline = "none";
    canvas.style.width = "100vw";
    canvas.style.height = "100vh";
    document.body.appendChild(canvas);
    const ctx = canvas.getContext("2d");
    Module.arcoGui.canvases[id] = canvas;
    Module.arcoGui.ctxs[id] = ctx;
    canvas.focus();

    const syncSize = function() {
        const w = Math.max(1, Math.round(canvas.clientWidth));
        const h = Math.max(1, Math.round(canvas.clientHeight));
        if (canvas.width === w && canvas.height === h) return;
        canvas.width = w;
        canvas.height = h;
        Module.arcoQueueEvent({type: "resize", id: id, width: w, height: h});
    };
    window.addEventListener("resize", syncSize);
    // First layout pass -- the CSS 100vw/100vh sizing above only takes effect once the element is
    // actually in the document, so the true initial size can differ from the width/height passed
    // to create_window (which is just what window_size() reports until the first sync/resize).
    syncSize();

    canvas.addEventListener("mousemove", function(e) {
        const rect = canvas.getBoundingClientRect();
        const x = e.clientX - rect.left;
        const y = e.clientY - rect.top;
        canvas.arcoLastPointer = {x: x, y: y};
        Module.arcoQueueEvent({type: "pointer-move", id: id, x: x, y: y});
    });
    const buttonName = function(e) { return e.button === 0 ? 0 : (e.button === 2 ? 1 : 2); };
    canvas.addEventListener("mousedown", function(e) {
        const rect = canvas.getBoundingClientRect();
        Module.arcoQueueEvent({type: "pointer-button", id: id, x: e.clientX - rect.left, y: e.clientY - rect.top,
            pressed: 1, button: buttonName(e), shift: e.shiftKey ? 1 : 0, ctrl: e.ctrlKey ? 1 : 0,
            alt: e.altKey ? 1 : 0, meta: e.metaKey ? 1 : 0});
        canvas.focus();
    });
    canvas.addEventListener("mouseup", function(e) {
        const rect = canvas.getBoundingClientRect();
        Module.arcoQueueEvent({type: "pointer-button", id: id, x: e.clientX - rect.left, y: e.clientY - rect.top,
            pressed: 0, button: buttonName(e), shift: e.shiftKey ? 1 : 0, ctrl: e.ctrlKey ? 1 : 0,
            alt: e.altKey ? 1 : 0, meta: e.metaKey ? 1 : 0});
    });
    canvas.addEventListener("contextmenu", function(e) { e.preventDefault(); });
    canvas.addEventListener("wheel", function(e) {
        const rect = canvas.getBoundingClientRect();
        Module.arcoQueueEvent({type: "scroll", id: id, dx: e.deltaX, dy: e.deltaY,
            x: e.clientX - rect.left, y: e.clientY - rect.top});
        e.preventDefault();
    }, {passive: false});
    canvas.addEventListener("keydown", function(e) {
        const name = Module.arcoKeyName(e);
        Module.arcoQueueEvent({type: "key", id: id, key: name, action: "press",
            shift: e.shiftKey ? 1 : 0, ctrl: e.ctrlKey ? 1 : 0, alt: e.altKey ? 1 : 0, meta: e.metaKey ? 1 : 0});
        if (e.key.length === 1 && !e.ctrlKey && !e.metaKey && !e.altKey) {
            Module.arcoQueueEvent({type: "text", id: id, text: e.key});
        }
        if (["Tab", "Backspace", "Delete", " ", "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight",
             "Home", "End", "PageUp", "PageDown"].indexOf(e.key) !== -1) {
            e.preventDefault();
        }
    });
    canvas.addEventListener("keyup", function(e) {
        const name = Module.arcoKeyName(e);
        Module.arcoQueueEvent({type: "key", id: id, key: name, action: "release",
            shift: e.shiftKey ? 1 : 0, ctrl: e.ctrlKey ? 1 : 0, alt: e.altKey ? 1 : 0, meta: e.metaKey ? 1 : 0});
    });
});

EM_JS(void, js_destroy_window, (int id), {
    const canvas = Module.arcoGui.canvases[id];
    if (canvas && canvas.parentNode) canvas.parentNode.removeChild(canvas);
    delete Module.arcoGui.canvases[id];
    delete Module.arcoGui.ctxs[id];
});

EM_JS(void, js_set_title, (const char* title_ptr), { document.title = UTF8ToString(title_ptr); });

EM_JS(int, js_canvas_width, (int id), { return Module.arcoGui.canvases[id].width; });
EM_JS(int, js_canvas_height, (int id), { return Module.arcoGui.canvases[id].height; });

EM_JS(void, js_clear, (int id, double r, double g, double b, double a), {
    const canvas = Module.arcoGui.canvases[id];
    const ctx = Module.arcoGui.ctxs[id];
    ctx.save();
    ctx.globalAlpha = 1.0;
    ctx.fillStyle = "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + "," + Math.round(b * 255) + "," + a + ")";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.restore();
});

EM_JS(void, js_fill_rect, (int id, double x, double y, double width, double height, double r, double g, double b, double a), {
    const ctx = Module.arcoGui.ctxs[id];
    ctx.fillStyle = "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + "," + Math.round(b * 255) + "," + a + ")";
    ctx.fillRect(x, y, width, height);
});

EM_JS(void, js_rounded_rect, (int id, double x, double y, double width, double height, double radius,
                              double r, double g, double b, double a), {
    const ctx = Module.arcoGui.ctxs[id];
    const rad = Math.max(0, Math.min(radius, Math.min(width, height) / 2));
    ctx.beginPath();
    ctx.moveTo(x + rad, y);
    ctx.arcTo(x + width, y, x + width, y + height, rad);
    ctx.arcTo(x + width, y + height, x, y + height, rad);
    ctx.arcTo(x, y + height, x, y, rad);
    ctx.arcTo(x, y, x + width, y, rad);
    ctx.closePath();
    ctx.fillStyle = "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + "," + Math.round(b * 255) + "," + a + ")";
    ctx.fill();
});

EM_JS(void, js_line, (int id, double x1, double y1, double x2, double y2, double thickness,
                      double r, double g, double b, double a), {
    const ctx = Module.arcoGui.ctxs[id];
    ctx.beginPath();
    ctx.moveTo(x1, y1);
    ctx.lineTo(x2, y2);
    ctx.lineWidth = Math.max(0.1, thickness);
    ctx.strokeStyle = "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + "," + Math.round(b * 255) + "," + a + ")";
    ctx.stroke();
});

EM_JS(void, js_circle, (int id, double cx, double cy, double radius, double r, double g, double b, double a), {
    const ctx = Module.arcoGui.ctxs[id];
    ctx.beginPath();
    ctx.arc(cx, cy, Math.max(0, radius), 0, Math.PI * 2);
    ctx.fillStyle = "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + "," + Math.round(b * 255) + "," + a + ")";
    ctx.fill();
});

EM_JS(void, js_pixel, (int id, int x, int y, double r, double g, double b, double a), {
    const ctx = Module.arcoGui.ctxs[id];
    ctx.fillStyle = "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + "," + Math.round(b * 255) + "," + a + ")";
    ctx.fillRect(x, y, 1, 1);
});

EM_JS(void, js_text, (int id, const char* text_ptr, double x, double y, double size, int mono,
                      double r, double g, double b, double a), {
    const ctx = Module.arcoGui.ctxs[id];
    ctx.font = Math.max(1, size) + "px " + (mono ? "monospace" : "sans-serif");
    ctx.textBaseline = "top";
    ctx.fillStyle = "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + "," + Math.round(b * 255) + "," + a + ")";
    ctx.fillText(UTF8ToString(text_ptr), x, y);
});

EM_JS(double, js_measure_text_width, (int id, const char* text_ptr, double size, int mono), {
    const ctx = Module.arcoGui.ctxs[id];
    ctx.font = Math.max(1, size) + "px " + (mono ? "monospace" : "sans-serif");
    return ctx.measureText(UTF8ToString(text_ptr)).width;
});

EM_JS(double, js_measure_text_height, (int id, const char* text_ptr, double size, int mono), {
    const ctx = Module.arcoGui.ctxs[id];
    ctx.font = Math.max(1, size) + "px " + (mono ? "monospace" : "sans-serif");
    const metrics = ctx.measureText(UTF8ToString(text_ptr));
    if (metrics.actualBoundingBoxAscent !== undefined && metrics.actualBoundingBoxDescent !== undefined) {
        const measured = metrics.actualBoundingBoxAscent + metrics.actualBoundingBoxDescent;
        if (measured > 0) return measured;
    }
    return size * 1.15;
});

EM_JS(void, js_set_clip, (int id, double x, double y, double width, double height), {
    const ctx = Module.arcoGui.ctxs[id];
    ctx.save();
    ctx.beginPath();
    ctx.rect(x, y, width, height);
    ctx.clip();
});

EM_JS(void, js_reset_clip, (int id), { Module.arcoGui.ctxs[id].restore(); });

EM_JS(void, js_set_cursor, (int id, const char* cursor_ptr), {
    Module.arcoGui.canvases[id].style.cursor = UTF8ToString(cursor_ptr);
});

EM_JS(void, js_get_pointer_position, (int id, double* out_xy), {
    const canvas = Module.arcoGui.canvases[id];
    const last = canvas.arcoLastPointer || {x: 0, y: 0};
    setValue(out_xy, last.x, "double");
    setValue(out_xy + 8, last.y, "double");
});

EM_JS(int, js_confirm, (const char* title_ptr, const char* message_ptr), {
    return window.confirm(UTF8ToString(title_ptr) + "\n\n" + UTF8ToString(message_ptr)) ? 1 : 0;
});

// window.prompt() rather than a real native file picker: see the file-level comment above. The
// path typed in is used exactly as File.ReadText/WriteText would use any other path, i.e. against
// Emscripten's in-memory MEMFS, not the user's real disk.
EM_JS(char*, js_prompt_path, (const char* title_ptr, const char* initial_ptr), {
    const result = window.prompt(UTF8ToString(title_ptr), UTF8ToString(initial_ptr));
    if (result === null) return 0;
    const length = lengthBytesUTF8(result) + 1;
    const buffer = _malloc(length);
    stringToUTF8(result, buffer, length);
    return buffer;
});

std::string prompt_path(const std::string& title, const std::string& initial_path) {
    char* result = js_prompt_path(title.c_str(), initial_path.c_str());
    if (!result) return "";
    std::string value(result);
    std::free(result);
    return value;
}

} // namespace

extern "C" {

void EMSCRIPTEN_KEEPALIVE arco_gui_push_pointer_move(int id, double x, double y) {
    events.emplace_back(Value::Object{{"Type", "pointer-move"}, {"Window", id}, {"X", x}, {"Y", y}});
}

void EMSCRIPTEN_KEEPALIVE arco_gui_push_pointer_button(int id, double x, double y, int pressed, int button,
                                                        int shift, int ctrl, int alt, int meta) {
    const char* name = button == 0 ? "left" : (button == 1 ? "right" : "middle");
    events.emplace_back(Value::Object{
        {"Type", "pointer-button"}, {"Window", id}, {"Action", pressed ? "press" : "release"},
        {"Button", name}, {"Clicks", 1}, {"X", x}, {"Y", y},
        {"Shift", shift != 0}, {"Ctrl", ctrl != 0}, {"Alt", alt != 0}, {"Super", meta != 0}});
}

void EMSCRIPTEN_KEEPALIVE arco_gui_push_key(int id, const char* key, const char* action,
                                            int shift, int ctrl, int alt, int meta) {
    const std::string name(key);
    const std::string act(action);
    if (act == "press") keys_down.insert(name); else keys_down.erase(name);
    events.emplace_back(Value::Object{
        {"Type", "key"}, {"Window", id}, {"Key", name}, {"Action", act},
        {"Shift", shift != 0}, {"Ctrl", ctrl != 0}, {"Alt", alt != 0}, {"Super", meta != 0}});
}

void EMSCRIPTEN_KEEPALIVE arco_gui_push_text(int id, const char* text) {
    events.emplace_back(Value::Object{{"Type", "text"}, {"Window", id}, {"Text", std::string(text)}});
}

void EMSCRIPTEN_KEEPALIVE arco_gui_push_scroll(int id, double dx, double dy, double x, double y) {
    events.emplace_back(Value::Object{{"Type", "scroll"}, {"Window", id}, {"DeltaX", dx}, {"DeltaY", dy}, {"X", x}, {"Y", y}});
}

void EMSCRIPTEN_KEEPALIVE arco_gui_push_resize(int id, int width, int height) {
    const auto found = windows.find(id);
    if (found != windows.end()) {
        found->second.width = width;
        found->second.height = height;
    }
    events.emplace_back(Value::Object{{"Type", "resize"}, {"Window", id}, {"Width", width}, {"Height", height}});
}

} // extern "C"

bool available() { return true; }
std::string backend() { return "canvas"; }

void set_application(const std::string&, const std::string&, const std::string&) {
    // Nothing to do -- a browser page has no separate "application identity" the way a desktop
    // window manager does; create_window's title sets document.title, which is the closest
    // equivalent.
}

int create_window(const std::string& title, int width, int height) {
    js_ensure_initialized();
    const int id = next_id++;
    windows[id] = WindowRecord{id, title, width, height};
    js_create_window(id, title.c_str(), width, height);
    return id;
}

void destroy_window(int id) {
    js_destroy_window(id);
    windows.erase(id);
}

bool should_close(int) { return false; }
void set_should_close(int, bool) {
    // A browser tab can't be closed programmatically by page script in the general case; nothing
    // meaningful to do here (unlike GLFW's should_close flag, there's no window-chrome close
    // button under the app's own control to react to either -- see the "close" event this backend
    // never emits).
}
void set_title(int id, const std::string& title) {
    find_window(id).title = title;
    js_set_title(title.c_str());
}

Value window_size(int id) {
    auto& item = find_window(id);
    return Value::Object{{"Width", item.width}, {"Height", item.height}};
}

void clear(int id, double r, double g, double b, double a) { js_clear(id, r, g, b, a); }
void pixel(int id, int x, int y, double r, double g, double b, double a) { js_pixel(id, x, y, r, g, b, a); }
void fill_rect(int id, double x, double y, double width, double height, double r, double g, double b, double a) {
    js_fill_rect(id, x, y, width, height, r, g, b, a);
}
void column(int id, int x, int y1, int y2, double r, double g, double b, double a) {
    if (y1 > y2) std::swap(y1, y2);
    js_fill_rect(id, x, y1, 1, y2 - y1 + 1, r, g, b, a);
}
void rectangle(int id, double x, double y, double width, double height, double r, double g, double b, double a) {
    js_fill_rect(id, x, y, width, height, r, g, b, a);
}
void rounded_rectangle(int id, double x, double y, double width, double height, double radius,
                       double r, double g, double b, double a) {
    js_rounded_rect(id, x, y, width, height, radius, r, g, b, a);
}
void line(int id, double x1, double y1, double x2, double y2, double thickness, double r, double g, double b, double a) {
    js_line(id, x1, y1, x2, y2, thickness, r, g, b, a);
}
void circle(int id, double center_x, double center_y, double radius, double r, double g, double b, double a) {
    js_circle(id, center_x, center_y, radius, r, g, b, a);
}
void text(int id, const std::string& value, double x, double y, double size, double r, double g, double b, double a) {
    js_text(id, value.c_str(), x, y, size, 0, r, g, b, a);
}
void text_mono(int id, const std::string& value, double x, double y, double size, double r, double g, double b, double a) {
    js_text(id, value.c_str(), x, y, size, 1, r, g, b, a);
}
void image(int, const std::string& path, double, double, double, double, double) {
    throw std::runtime_error("GUI.Image is not yet supported by the web capsule target (image "
                              "loading is asynchronous in a browser; " + path + " could not be drawn)");
}
Value measure_text(int id, const std::string& value, double size) {
    return Value::Object{{"Width", js_measure_text_width(id, value.c_str(), size, 0)},
                         {"Height", js_measure_text_height(id, value.c_str(), size, 0)}};
}
Value measure_text_mono(int id, const std::string& value, double size) {
    return Value::Object{{"Width", js_measure_text_width(id, value.c_str(), size, 1)},
                         {"Height", js_measure_text_height(id, value.c_str(), size, 1)}};
}
void set_clip(int id, double x, double y, double width, double height) { js_set_clip(id, x, y, width, height); }
void reset_clip(int id) { js_reset_clip(id); }
std::string clipboard_text(int) { return ""; }
void set_clipboard_text(int, const std::string&) {
    // Real clipboard access (navigator.clipboard) is async/permission-gated in browsers; silently
    // dropped rather than thrown, since Ctrl+C/Ctrl+V-style keyboard shortcuts routing here from
    // an editor widget shouldn't hard-crash the app over a clipboard that just isn't wired up yet.
}
void set_cursor(int id, const std::string& cursor) {
    std::string css = "default";
    if (cursor == "text") css = "text";
    else if (cursor == "hand") css = "pointer";
    else if (cursor != "default" && cursor != "arrow") throw std::runtime_error("unknown GUI cursor: " + cursor);
    js_set_cursor(id, css.c_str());
}
bool key_down(int, const std::string& key) {
    js_flush_pending_events();
    return keys_down.count(key) != 0;
}
Value pointer_position(int id) {
    (void)find_window(id);
    double xy[2] = {0, 0};
    js_get_pointer_position(id, xy);
    return Value::Object{{"X", xy[0]}, {"Y", xy[1]}};
}
std::string open_file_dialog(int, const std::string& title, const std::string& initial_path) {
    return prompt_path(title.empty() ? "Open path" : title, initial_path);
}
std::string save_file_dialog(int, const std::string& title, const std::string& initial_path) {
    return prompt_path(title.empty() ? "Save path" : title, initial_path);
}
bool confirm(int, const std::string& title, const std::string& message) {
    return js_confirm(title.c_str(), message.c_str()) != 0;
}
void present(int) {
    // Canvas 2D composites immediately as each draw call happens -- there's no separate
    // back-buffer "swap" the way GLFW's present() needs (glfwSwapBuffers). Yielding once still
    // gives the browser a scheduling point to actually paint the frame just drawn before the next
    // WaitEvent poll loop starts spinning again.
    emscripten_sleep(0);
}
Value poll_event() {
    if (!initialized) { js_ensure_initialized(); initialized = true; }
    js_flush_pending_events();
    if (events.empty()) return no_event();
    Value event = events.front();
    events.pop_front();
    return event;
}
Value wait_event(double timeout_seconds) {
    if (!initialized) { js_ensure_initialized(); initialized = true; }
    js_flush_pending_events();
    if (events.empty()) {
        const double step_ms = 4.0;
        double waited_ms = 0.0;
        const double budget_ms = std::max(0.0, timeout_seconds) * 1000.0;
        while (events.empty() && waited_ms < budget_ms) {
            emscripten_sleep(static_cast<unsigned int>(step_ms));
            // Safe here specifically because we're back in a normal (rewound) C++ frame after
            // emscripten_sleep returns, not inside a JS listener callback while a sleep is still
            // in flight -- see the comment on Module.arcoQueueEvent/arcoFlushPendingEvents in
            // js_ensure_initialized above for why that distinction is the whole point.
            js_flush_pending_events();
            waited_ms += step_ms;
        }
    }
    if (events.empty()) return no_event();
    Value event = events.front();
    events.pop_front();
    return event;
}

} // namespace arco::gui
