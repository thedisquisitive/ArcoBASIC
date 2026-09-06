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
// open_file_dialog/save_file_dialog use the real File System Access API (showOpenFilePicker/
// showSaveFilePicker) when the browser supports it -- a genuine native OS file dialog reading from
// and writing to the user's actual disk, not just Emscripten's ephemeral in-memory MEMFS. Open
// copies the picked file's bytes into MEMFS at a synthetic path and hands that path back, so
// File.ReadText keeps working unchanged; Save stashes the real FileSystemFileHandle and syncs
// MEMFS's content out to disk via an FS.trackingDelegate.onCloseFile hook the moment
// File.WriteText's own fclose() lands (see js_ensure_initialized and js_pick_open_file/
// js_pick_save_file below for the full mechanics). Browsers without the API (Firefox, Safari as of
// this writing) fall back to a window.prompt()-based path into MEMFS, same as before.
//
// Known, deliberate gaps for this first pass (each throws a clear error rather than silently
// misbehaving): GUI.Image (loading an image file is inherently asynchronous in a browser; wiring
// that through Asyncify + fetch is a real follow-up, not done here). Clipboard access is similarly
// a no-op stub (real clipboard access is async/permission-gated in browsers).

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
    Module.arcoGui = {canvases: {}, ctxs: {}, saveHandles: {}};
    // Bridges a real FileSystemFileHandle (from showSaveFilePicker, see js_pick_save_file below)
    // back out to the user's actual disk. File.WriteText has no idea any of this exists -- it just
    // does a normal fopen/fwrite/fclose against MEMFS, the same as it would for any other path.
    // Emscripten's FS.trackingDelegate.onCloseFile fires for *every* MEMFS file close regardless of
    // C++ call site, so registering one here is enough to notice "a write just landed at a path we
    // have a real handle for" without touching File.WriteText or the arco::gui interface at all.
    // Fire-and-forget async: the real disk write finishes a moment after File.WriteText returns,
    // not before -- fine for ArcoBASIC source files, which are tiny.
    if (typeof FS !== "undefined" && FS.trackingDelegate) {
        FS.trackingDelegate["onCloseFile"] = function(path) {
            const handle = Module.arcoGui.saveHandles[path];
            if (!handle) return;
            delete Module.arcoGui.saveHandles[path];
            try {
                const data = FS.readFile(path);
                (async function() {
                    try {
                        const writable = await handle.createWritable();
                        await writable.write(data);
                        await writable.close();
                    } catch (e) {
                        console.error("ArcoFlow: failed to sync saved file to disk:", e);
                    }
                })();
            } catch (e) {
                console.error("ArcoFlow: failed to read back saved file for disk sync:", e);
            }
        };
    }
    // Shared fallback for open_file_dialog/save_file_dialog: window.prompt() for a path into
    // Emscripten's ephemeral MEMFS. Used when the File System Access API isn't available at all
    // (Firefox, Safari as of this writing), and also as a safety net if a real picker call throws
    // for a reason other than the user genuinely cancelling it (e.g. a SecurityError from the
    // "must be handling a user gesture" requirement -- see js_pick_open_file/js_pick_save_file)
    // so Open/Save never just silently do nothing with no way for the user to proceed.
    Module.arcoPromptPath = function(title, initialPath) {
        const result = window.prompt(title, initialPath);
        return (result === null || result === "") ? null : result;
    };
    Module.arcoKeyName = function(e) {
        const key = e.key;
        // Named lookup first, length-1 fallback second -- " " (the spacebar's e.key) has length 1
        // just like any regular character key, so checking length first would return the literal
        // " " character instead of "space" and never reach the table entry that maps it correctly.
        const named = {
            Escape: "escape", Enter: "enter", " ": "space", Spacebar: "space",
            ArrowLeft: "left", ArrowRight: "right", ArrowUp: "up", ArrowDown: "down",
            Home: "home", End: "end", PageUp: "pageup", PageDown: "pagedown",
            Tab: "tab", Backspace: "backspace", Delete: "delete",
        };
        if (named[key]) return named[key];
        if (key.length === 1) return key.toLowerCase();
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

    // Touch input -- translated into the exact same pointer-move/pointer-button events the mouse
    // listeners above already produce, so nothing at the ArcoBASIC or C++ level needs to know
    // touch exists at all (a Slider's drag handling, a Button's click test, ... all just see
    // "pointer" events regardless of which input made them). Single-touch only: these are 2D
    // single-pointer games and widgets, multi-touch gestures are out of scope. preventDefault on
    // every touch event keeps the browser's own scroll/pinch-zoom/text-selection from fighting
    // with dragging a slider or tapping a button.
    const touchPoint = function(e) {
        const touch = e.touches[0] || e.changedTouches[0];
        const rect = canvas.getBoundingClientRect();
        return {x: touch.clientX - rect.left, y: touch.clientY - rect.top};
    };
    canvas.addEventListener("touchstart", function(e) {
        e.preventDefault();
        const point = touchPoint(e);
        canvas.arcoLastPointer = point;
        Module.arcoQueueEvent({type: "pointer-move", id: id, x: point.x, y: point.y});
        Module.arcoQueueEvent({type: "pointer-button", id: id, x: point.x, y: point.y,
            pressed: 1, button: 0, shift: 0, ctrl: 0, alt: 0, meta: 0});
        canvas.focus();
    }, {passive: false});
    canvas.addEventListener("touchmove", function(e) {
        e.preventDefault();
        const point = touchPoint(e);
        canvas.arcoLastPointer = point;
        Module.arcoQueueEvent({type: "pointer-move", id: id, x: point.x, y: point.y});
    }, {passive: false});
    const touchEnd = function(e) {
        e.preventDefault();
        const point = touchPoint(e);
        Module.arcoQueueEvent({type: "pointer-button", id: id, x: point.x, y: point.y,
            pressed: 0, button: 0, shift: 0, ctrl: 0, alt: 0, meta: 0});
    };
    canvas.addEventListener("touchend", touchEnd, {passive: false});
    canvas.addEventListener("touchcancel", touchEnd, {passive: false});
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
    // Reset any scale set by a previous frame's GUI.SetScale before clearing -- fillRect's
    // coordinates go through the current transform like any other draw call, so clearing while
    // still scaled down would only clear a shrunk rectangle in the corner, not the full canvas.
    // A game that wants scaling calls GUI.SetScale again right after GUI.Clear each frame, so
    // resetting here just means "clear always covers everything, scale is re-established after."
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.save();
    ctx.globalAlpha = 1.0;
    ctx.fillStyle = "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + "," + Math.round(b * 255) + "," + a + ")";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.restore();
});

EM_JS(void, js_set_scale, (int id, double factor), {
    const ctx = Module.arcoGui.ctxs[id];
    ctx.setTransform(factor, 0, 0, factor, 0, 0);
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

EM_JS(void, js_fill_polygon, (int id, const double* points_ptr, int count, double r, double g, double b, double a), {
    if (count < 3) return;
    const ctx = Module.arcoGui.ctxs[id];
    const points = Module.HEAPF64.subarray(points_ptr / 8, points_ptr / 8 + count * 2);
    ctx.beginPath();
    ctx.moveTo(points[0], points[1]);
    for (let i = 1; i < count; i++) {
        ctx.lineTo(points[i * 2], points[i * 2 + 1]);
    }
    ctx.closePath();
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

// Real native pickers via the File System Access API (showOpenFilePicker/showSaveFilePicker),
// bridged through Asyncify: EM_ASYNC_JS lets this JS body `await` the picker's Promise while
// looking to the C++ caller like an ordinary blocking function call, exactly like emscripten_sleep
// elsewhere in this file -- it works because the whole capsule already links with -sASYNCIFY.
//
// Open reads the picked file's bytes straight into MEMFS at a synthetic path and hands that path
// back, so the existing File.ReadText(path) call in arcoflow.abas keeps working completely
// unchanged. Save is trickier: ArcoBASIC's GUI.SaveFileDialog/File.WriteText are two separate
// calls (get a path, then separately write to it), but showSaveFilePicker's real disk handle only
// exists at the *first* call -- so the handle gets stashed in Module.arcoGui.saveHandles, keyed by
// the same synthetic path, and js_ensure_initialized's FS.trackingDelegate.onCloseFile hook above
// flushes MEMFS's content out to the real handle the moment File.WriteText's own fclose() fires.
//
// Falls back to Module.arcoPromptPath (window.prompt() into MEMFS) both when the API doesn't exist
// at all (Firefox, Safari as of this writing) and when a real picker call throws for any reason
// *other* than the user genuinely dismissing it (their spec-mandated AbortError) -- most notably
// SecurityError ("Must be handling a user gesture to show a file picker"), which showed up in
// testing whenever the click-to-picker path lands outside the browser's transient-activation
// window (e.g. CDP-dispatched clicks in headless testing; conceivably real usage too, depending on
// how long GUI.WaitEvent's poll loop takes to drain the click before dispatching it to Open/Save).
// Without this fallback that's a silent dead end -- the button visibly does nothing and there's no
// way for the user to tell why. With it, Open/Save always resolve to either a real result or an
// explicit cancel, never a mysterious no-op.
EM_ASYNC_JS(char*, js_pick_open_file, (const char* title_ptr, const char* initial_ptr), {
    const title = UTF8ToString(title_ptr);
    const initial = UTF8ToString(initial_ptr);
    let path = null;
    if (typeof window.showOpenFilePicker === "function") {
        try {
            const [handle] = await window.showOpenFilePicker({multiple: false});
            const file = await handle.getFile();
            const bytes = new Uint8Array(await file.arrayBuffer());
            path = "/tmp/arcoflow-opened-" + Date.now() + "-" + file.name;
            FS.writeFile(path, bytes);
        } catch (e) {
            if (e.name === "AbortError") return 0; // user genuinely cancelled -- no fallback
            console.warn("ArcoFlow: native file picker unavailable (" + e.name + "), falling back to a path prompt:", e);
            path = Module.arcoPromptPath(title, initial);
        }
    } else {
        path = Module.arcoPromptPath(title, initial);
    }
    if (path === null) return 0;
    const length = lengthBytesUTF8(path) + 1;
    const buffer = _malloc(length);
    stringToUTF8(path, buffer, length);
    return buffer;
});

EM_ASYNC_JS(char*, js_pick_save_file, (const char* title_ptr, const char* initial_ptr, const char* suggested_name_ptr), {
    const title = UTF8ToString(title_ptr);
    const initial = UTF8ToString(initial_ptr);
    const suggestedName = UTF8ToString(suggested_name_ptr);
    let path = null;
    if (typeof window.showSaveFilePicker === "function") {
        try {
            const options = suggestedName ? {suggestedName: suggestedName} : {};
            const handle = await window.showSaveFilePicker(options);
            path = "/tmp/arcoflow-save-" + Date.now() + "-" + handle.name;
            Module.arcoGui.saveHandles[path] = handle;
            FS.writeFile(path, new Uint8Array(0)); // placeholder until the first real File.WriteText
        } catch (e) {
            if (e.name === "AbortError") return 0; // user genuinely cancelled -- no fallback
            console.warn("ArcoFlow: native save picker unavailable (" + e.name + "), falling back to a path prompt:", e);
            path = Module.arcoPromptPath(title, initial);
        }
    } else {
        path = Module.arcoPromptPath(title, initial);
    }
    if (path === null) return 0;
    const length = lengthBytesUTF8(path) + 1;
    const buffer = _malloc(length);
    stringToUTF8(path, buffer, length);
    return buffer;
});

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

// A browser <canvas> has no OS-level window chrome, transparency, or click-through concept to
// shape (RFC-ArcoUI section 9.3/42's own allowance that not every backend need implement shape
// identically) -- frameless/transparent are accepted and ignored so ArcoUI's stdlib layer can
// call the same create_window signature on every backend; supports_shaped_windows() below is how
// a caller finds out ahead of time that this backend's shapes are draw-only, not real click-through.
int create_window(const std::string& title, int width, int height, bool /*frameless*/, bool /*transparent*/) {
    return create_window(title, width, height);
}

bool supports_shaped_windows() { return false; }

void fill_polygon(int id, const std::vector<std::pair<double, double>>& points,
                  double r, double g, double b, double a) {
    if (points.size() < 3) return;
    std::vector<double> flat;
    flat.reserve(points.size() * 2);
    for (const auto& point : points) {
        flat.push_back(point.first);
        flat.push_back(point.second);
    }
    js_fill_polygon(id, flat.data(), static_cast<int>(points.size()), r, g, b, a);
}

// No OS window to make click-through here (see supports_shaped_windows()) -- a no-op, not an
// error, so ArcoUI's stdlib layer doesn't need a backend-specific branch just to call this safely.
void set_input_passthrough(int, bool) {}

// No OS window position to report or change for an in-page <canvas> -- same no-op convention as
// set_input_passthrough above.
Value window_position(int) { return Value::Object{{"X", 0}, {"Y", 0}}; }
void set_window_position(int, int, int) {}

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
void set_scale(int id, double factor) { js_set_scale(id, factor); }
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
    char* result = js_pick_open_file((title.empty() ? "Open path" : title).c_str(), initial_path.c_str());
    if (!result) return "";
    std::string value(result);
    std::free(result);
    return value;
}
std::string save_file_dialog(int, const std::string& title, const std::string& initial_path) {
    // Suggest just the basename -- a directory component wouldn't mean anything to the browser's
    // own picker, which starts in the user's last-used (or default Downloads) directory, the same
    // as any other native save dialog.
    std::string suggested = initial_path;
    const auto slash = suggested.find_last_of('/');
    if (slash != std::string::npos) suggested = suggested.substr(slash + 1);
    char* result = js_pick_save_file((title.empty() ? "Save path" : title).c_str(), initial_path.c_str(),
                                      suggested.c_str());
    if (!result) return "";
    std::string value(result);
    std::free(result);
    return value;
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
void clear_3d(int, double, double, double, double, double, double, double, double, double, double, double, double, double, double, double) {
    // Deliberately unimplemented for the web capsule target for now: real depth-tested 3D
    // (glfw_backend.cpp's own clear_3d()/triangle_3d()) needs a real WebGL context, not the 2D
    // canvas this backend draws through -- a separate increment, not silently faked here as a 2D
    // approximation.
    throw std::runtime_error("GUI.Clear3D is not yet supported on the web capsule target");
}
void triangle_3d(int, double, double, double, double, double, double, double, double, double, double, double, double, double) {
    throw std::runtime_error("GUI.Triangle3D is not yet supported on the web capsule target");
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
