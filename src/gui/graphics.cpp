#include "arco/graphics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <cstdlib>

namespace arco::graphics {
namespace {

constexpr std::uint32_t kBytesPerPixel = 4;

struct Binding { Surface* surface = nullptr; std::weak_ptr<void> lifetime; };
thread_local std::vector<Binding> bindings;

bool supported(PixelFormat format) {
    return format == PixelFormat::RedGreenBlueReserved8 || format == PixelFormat::BlueGreenRedReserved8;
}

std::array<std::uint8_t, 5> glyph(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    switch (c) {
    case 'A': return {0x1E, 0x05, 0x05, 0x1E, 0}; case 'B': return {0x1F, 0x15, 0x15, 0x0A, 0};
    case 'C': return {0x0E, 0x11, 0x11, 0x11, 0}; case 'D': return {0x1F, 0x11, 0x11, 0x0E, 0};
    case 'E': return {0x1F, 0x15, 0x15, 0x11, 0}; case 'F': return {0x1F, 0x05, 0x05, 0x01, 0};
    case 'G': return {0x0E, 0x11, 0x15, 0x1D, 0}; case 'H': return {0x1F, 0x04, 0x04, 0x1F, 0};
    case 'I': return {0x11, 0x1F, 0x11, 0, 0}; case 'J': return {0x08, 0x10, 0x10, 0x0F, 0};
    case 'K': return {0x1F, 0x04, 0x0A, 0x11, 0}; case 'L': return {0x1F, 0x10, 0x10, 0x10, 0};
    case 'M': return {0x1F, 0x02, 0x04, 0x02, 0x1F}; case 'N': return {0x1F, 0x02, 0x04, 0x1F, 0};
    case 'O': return {0x0E, 0x11, 0x11, 0x0E, 0}; case 'P': return {0x1F, 0x05, 0x05, 0x02, 0};
    case 'Q': return {0x0E, 0x11, 0x19, 0x1E, 0}; case 'R': return {0x1F, 0x05, 0x0D, 0x12, 0};
    case 'S': return {0x12, 0x15, 0x15, 0x09, 0}; case 'T': return {0x01, 0x1F, 0x01, 0, 0};
    case 'U': return {0x0F, 0x10, 0x10, 0x0F, 0}; case 'V': return {0x07, 0x18, 0x18, 0x07, 0};
    case 'W': return {0x1F, 0x08, 0x04, 0x08, 0x1F}; case 'X': return {0x1B, 0x04, 0x04, 0x1B, 0};
    case 'Y': return {0x03, 0x1C, 0x03, 0, 0}; case 'Z': return {0x19, 0x15, 0x13, 0, 0};
    case '0': return {0x0E, 0x11, 0x11, 0x0E, 0}; case '1': return {0x12, 0x1F, 0x10, 0, 0};
    case '2': return {0x19, 0x15, 0x15, 0x12, 0}; case '3': return {0x11, 0x15, 0x15, 0x0A, 0};
    case '4': return {0x07, 0x04, 0x1F, 0x04, 0}; case '5': return {0x17, 0x15, 0x15, 0x09, 0};
    case '6': return {0x0E, 0x15, 0x15, 0x08, 0}; case '7': return {0x01, 0x1D, 0x03, 0, 0};
    case '8': return {0x0A, 0x15, 0x15, 0x0A, 0}; case '9': return {0x02, 0x15, 0x15, 0x0E, 0};
    case '-': return {0x04, 0x04, 0x04, 0, 0}; case '.': return {0, 0x10, 0x10, 0, 0};
    case ':': return {0, 0x0A, 0, 0x0A, 0}; case '[': return {0x1F, 0x11, 0, 0, 0};
    case ']': return {0x11, 0x1F, 0, 0, 0}; case '>': return {0x04, 0x0A, 0x11, 0, 0};
    case '/': return {0x10, 0x08, 0x04, 0x02, 0x01}; case ' ': return {0, 0, 0, 0, 0};
    default: return {0x1F, 0x11, 0x15, 0x1F, 0};
    }
}

} // namespace

bool Surface::valid() const {
    return Width != 0 && Height != 0 && PixelsPerScanLine >= Width && Pixels != nullptr && supported(static_cast<PixelFormat>(PixelFormatValue));
}

std::size_t Surface::byte_size() const {
    if (PixelsPerScanLine == 0 || Height > std::numeric_limits<std::size_t>::max() / PixelsPerScanLine ||
        static_cast<std::size_t>(PixelsPerScanLine) * Height > std::numeric_limits<std::size_t>::max() / kBytesPerPixel)
        return 0;
    return static_cast<std::size_t>(PixelsPerScanLine) * Height * kBytesPerPixel;
}

SurfaceResult CreateSurface(std::uint32_t width, std::uint32_t height, PixelFormat format) {
    if (width == 0 || height == 0) return {{}, SurfaceError::InvalidDimensions};
    if (!supported(format)) return {{}, SurfaceError::UnsupportedPixelFormat};
    const std::uint64_t bytes = static_cast<std::uint64_t>(width) * height * kBytesPerPixel;
    if (bytes > std::numeric_limits<std::size_t>::max()) return {{}, SurfaceError::SizeOverflow};
    try {
        auto storage = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(bytes), 0);
        Surface surface{width, height, width, static_cast<std::uint32_t>(format), storage->data(), false, storage, storage};
        return {std::move(surface), SurfaceError::None};
    } catch (const std::bad_alloc&) {
        return {{}, SurfaceError::AllocationFailure};
    }
}

SurfaceResult FromFramebuffer(volatile std::uint8_t* pixels, std::uint32_t width, std::uint32_t height,
                              std::uint32_t stride, PixelFormat format) {
    if (width == 0 || height == 0) return {{}, SurfaceError::InvalidDimensions};
    if (pixels == nullptr) return {{}, SurfaceError::NullPixels};
    if (stride < width) return {{}, SurfaceError::InvalidStride};
    if (!supported(format)) return {{}, SurfaceError::UnsupportedPixelFormat};
    auto lifetime = std::make_shared<int>(0);
    Surface surface{width, height, stride, static_cast<std::uint32_t>(format), pixels, true, {}, lifetime};
    return {std::move(surface), SurfaceError::None};
}

std::optional<std::uint32_t> PackColor(Color color, PixelFormat format) {
    if (!supported(format)) return std::nullopt;
    // GOP's 8-bit-per-color formats ignore the reserved/alpha byte.
    if (format == PixelFormat::RedGreenBlueReserved8)
        return static_cast<std::uint32_t>(color.r) | (static_cast<std::uint32_t>(color.g) << 8) | (static_cast<std::uint32_t>(color.b) << 16);
    return static_cast<std::uint32_t>(color.b) | (static_cast<std::uint32_t>(color.g) << 8) | (static_cast<std::uint32_t>(color.r) << 16);
}

bool PutPixel(Surface& surface, std::int32_t x, std::int32_t y, Color color) {
    if (!surface.valid() || x < 0 || y < 0 || static_cast<std::uint32_t>(x) >= surface.Width || static_cast<std::uint32_t>(y) >= surface.Height) return false;
    const auto packed = PackColor(color, static_cast<PixelFormat>(surface.PixelFormatValue));
    if (!packed) return false;
    const std::size_t offset = (static_cast<std::size_t>(y) * surface.PixelsPerScanLine + static_cast<std::size_t>(x)) * kBytesPerPixel;
    surface.Pixels[offset + 0] = static_cast<std::uint8_t>(*packed);
    surface.Pixels[offset + 1] = static_cast<std::uint8_t>(*packed >> 8);
    surface.Pixels[offset + 2] = static_cast<std::uint8_t>(*packed >> 16);
    surface.Pixels[offset + 3] = 0;
    return true;
}

void DrawLine(Surface& surface, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, Color color) {
    const std::int64_t dx = std::llabs(static_cast<std::int64_t>(x1) - x0), sx = x0 < x1 ? 1 : -1;
    const std::int64_t dy = -std::llabs(static_cast<std::int64_t>(y1) - y0), sy = y0 < y1 ? 1 : -1;
    std::int64_t err = dx + dy;
    for (;;) {
        PutPixel(surface, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const std::int64_t twice = 2 * err;
        if (twice >= dy) { err += dy; x0 += static_cast<std::int32_t>(sx); }
        if (twice <= dx) { err += dx; y0 += static_cast<std::int32_t>(sy); }
    }
}

void FillRect(Surface& surface, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height, Color color) {
    if (width <= 0 || height <= 0 || !surface.valid()) return;
    const std::int64_t left = std::max<std::int64_t>(0, x), top = std::max<std::int64_t>(0, y);
    const std::int64_t right = std::min<std::int64_t>(surface.Width, static_cast<std::int64_t>(x) + width);
    const std::int64_t bottom = std::min<std::int64_t>(surface.Height, static_cast<std::int64_t>(y) + height);
    for (std::int64_t py = top; py < bottom; ++py)
        for (std::int64_t px = left; px < right; ++px) PutPixel(surface, static_cast<std::int32_t>(px), static_cast<std::int32_t>(py), color);
}

void DrawRect(Surface& surface, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height, Color color) {
    if (width <= 0 || height <= 0) return;
    DrawLine(surface, x, y, x + width - 1, y, color); DrawLine(surface, x, y + height - 1, x + width - 1, y + height - 1, color);
    DrawLine(surface, x, y, x, y + height - 1, color); DrawLine(surface, x + width - 1, y, x + width - 1, y + height - 1, color);
}

void DrawCircle(Surface& surface, std::int32_t cx, std::int32_t cy, std::int32_t radius, Color color) {
    if (radius < 0) return;
    std::int32_t x = radius, y = 0, error = 1 - radius;
    while (x >= y) {
        PutPixel(surface, cx + x, cy + y, color); PutPixel(surface, cx + y, cy + x, color); PutPixel(surface, cx - y, cy + x, color); PutPixel(surface, cx - x, cy + y, color);
        PutPixel(surface, cx - x, cy - y, color); PutPixel(surface, cx - y, cy - x, color); PutPixel(surface, cx + y, cy - x, color); PutPixel(surface, cx + x, cy - y, color);
        ++y; if (error < 0) error += 2 * y + 1; else { --x; error += 2 * (y - x) + 1; }
    }
}

void FillCircle(Surface& surface, std::int32_t cx, std::int32_t cy, std::int32_t radius, Color color) {
    if (radius < 0) return;
    for (std::int32_t y = -radius; y <= radius; ++y) {
        const std::int64_t span = static_cast<std::int64_t>(radius) * radius - static_cast<std::int64_t>(y) * y;
        const auto half = static_cast<std::int32_t>(std::sqrt(static_cast<double>(span)));
        DrawLine(surface, cx - half, cy + y, cx + half, cy + y, color);
    }
}

TextMetrics MeasureText(std::string_view text, std::uint32_t glyph_width, std::uint32_t line_height) {
    std::uint32_t line = 0, max_line = 0, lines = 1;
    for (const char c : text) { if (c == '\n') { max_line = std::max(max_line, line); line = 0; ++lines; } else line += glyph_width; }
    return {std::max(max_line, line), lines * line_height};
}

void DrawText(Surface& surface, std::int32_t x, std::int32_t y, std::string_view text, Color color, std::uint32_t glyph_width, std::uint32_t line_height) {
    std::int32_t cursor_x = x, cursor_y = y;
    for (const char c : text) {
        if (c == '\n') { cursor_x = x; cursor_y += static_cast<std::int32_t>(line_height); continue; }
        const auto bits = glyph(c);
        for (std::int32_t gy = 0; gy < 7; ++gy) for (std::int32_t gx = 0; gx < 5; ++gx)
            if (bits[static_cast<std::size_t>(gx)] & (1U << gy)) PutPixel(surface, cursor_x + gx, cursor_y + gy, color);
        cursor_x += static_cast<std::int32_t>(glyph_width);
    }
}

bool Blit(const Surface& source, Surface& destination, std::int32_t x, std::int32_t y) {
    if (!source.valid() || !destination.valid() || source.PixelFormatValue != destination.PixelFormatValue) return false;
    const std::int32_t sx = std::max(0, -x), sy = std::max(0, -y);
    const std::int32_t dx = std::max(0, x), dy = std::max(0, y);
    const std::int32_t width = std::min<std::int32_t>(source.Width - sx, destination.Width - dx);
    const std::int32_t height = std::min<std::int32_t>(source.Height - sy, destination.Height - dy);
    if (width <= 0 || height <= 0) return true;
    for (std::int32_t row = 0; row < height; ++row) {
        const auto* from = source.Pixels + (static_cast<std::size_t>(sy + row) * source.PixelsPerScanLine + sx) * kBytesPerPixel;
        volatile auto* to = destination.Pixels + (static_cast<std::size_t>(dy + row) * destination.PixelsPerScanLine + dx) * kBytesPerPixel;
        for (std::size_t byte = 0; byte < static_cast<std::size_t>(width) * kBytesPerPixel; ++byte) to[byte] = from[byte];
    }
    return true;
}

bool Present(const Surface& backbuffer, Surface& framebuffer) {
    if (!backbuffer.valid() || !framebuffer.valid() || backbuffer.PixelFormatValue != framebuffer.PixelFormatValue ||
        backbuffer.Width != framebuffer.Width || backbuffer.Height != framebuffer.Height) return false;
    return Blit(backbuffer, framebuffer, 0, 0);
}

bool Bind(Surface& surface) {
    if (!surface.valid()) return false;
    bindings.clear();
    bindings.push_back({&surface, surface.Lifetime});
    return true;
}

bool PushSurface(Surface& surface) {
    if (!surface.valid()) return false;
    bindings.push_back({&surface, surface.Lifetime});
    return true;
}

bool PopSurface() {
    if (bindings.empty()) return false;
    bindings.pop_back();
    return true;
}

void Unbind() { bindings.clear(); }

Surface* CurrentSurface() {
    while (!bindings.empty()) {
        const auto& binding = bindings.back();
        if (binding.surface != nullptr && !binding.lifetime.expired() && binding.surface->valid()) return binding.surface;
        bindings.pop_back();
    }
    return nullptr;
}

bool PutPixel(std::int32_t x, std::int32_t y, Color color) { auto* surface = CurrentSurface(); return surface != nullptr && PutPixel(*surface, x, y, color); }
void DrawLine(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, Color color) { if (auto* s = CurrentSurface()) DrawLine(*s, x0, y0, x1, y1, color); }
void DrawRect(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height, Color color) { if (auto* s = CurrentSurface()) DrawRect(*s, x, y, width, height, color); }
void FillRect(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height, Color color) { if (auto* s = CurrentSurface()) FillRect(*s, x, y, width, height, color); }
void DrawCircle(std::int32_t cx, std::int32_t cy, std::int32_t radius, Color color) { if (auto* s = CurrentSurface()) DrawCircle(*s, cx, cy, radius, color); }
void FillCircle(std::int32_t cx, std::int32_t cy, std::int32_t radius, Color color) { if (auto* s = CurrentSurface()) FillCircle(*s, cx, cy, radius, color); }
void DrawText(std::int32_t x, std::int32_t y, std::string_view text, Color color, std::uint32_t glyph_width, std::uint32_t line_height) { if (auto* s = CurrentSurface()) DrawText(*s, x, y, text, color, glyph_width, line_height); }
bool Blit(const Surface& source, std::int32_t x, std::int32_t y) { auto* destination = CurrentSurface(); return destination != nullptr && Blit(source, *destination, x, y); }

Rect Centered(Rect parent, std::int32_t width, std::int32_t height) { return {parent.x + (parent.width - width) / 2, parent.y + (parent.height - height) / 2, width, height}; }
Rect Align(Rect parent, std::int32_t width, std::int32_t height, HorizontalAlign h, VerticalAlign v) {
    const auto x = h == HorizontalAlign::Left ? parent.x : h == HorizontalAlign::Right ? parent.x + parent.width - width : parent.x + (parent.width - width) / 2;
    const auto y = v == VerticalAlign::Top ? parent.y : v == VerticalAlign::Bottom ? parent.y + parent.height - height : parent.y + (parent.height - height) / 2;
    return {x, y, width, height};
}

void Render(Surface& surface, const Label& label) { DrawText(surface, label.bounds.x, label.bounds.y, label.text, label.color); }
void Render(Surface& surface, const Panel& panel) { FillRect(surface, panel.bounds.x, panel.bounds.y, panel.bounds.width, panel.bounds.height, panel.background); DrawRect(surface, panel.bounds.x, panel.bounds.y, panel.bounds.width, panel.bounds.height, panel.border); }
void Render(Surface& surface, const Button& button) { FillRect(surface, button.bounds.x, button.bounds.y, button.bounds.width, button.bounds.height, button.background); DrawRect(surface, button.bounds.x, button.bounds.y, button.bounds.width, button.bounds.height, Color::White()); DrawText(surface, button.bounds.x + 6, button.bounds.y + 6, button.text, button.foreground); }
void Render(Surface& surface, const ProgressBar& progress) {
    FillRect(surface, progress.bounds.x, progress.bounds.y, progress.bounds.width, progress.bounds.height, progress.track);
    const auto clamped = std::min(progress.value, progress.maximum);
    const auto filled = progress.maximum == 0 ? 0 : static_cast<std::int32_t>((static_cast<std::uint64_t>(progress.bounds.width) * clamped) / progress.maximum);
    FillRect(surface, progress.bounds.x, progress.bounds.y, filled, progress.bounds.height, progress.fill);
}

} // namespace arco::graphics
