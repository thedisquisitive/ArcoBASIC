#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace arco::graphics {

enum class PixelFormat : std::uint32_t {
    RedGreenBlueReserved8 = 0,
    BlueGreenRedReserved8 = 1,
};

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    static constexpr Color RGB(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
        return {red, green, blue, 255};
    }
    static constexpr Color RGBA(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha) {
        return {red, green, blue, alpha};
    }
    static constexpr Color Black() { return RGB(0, 0, 0); }
    static constexpr Color White() { return RGB(255, 255, 255); }
    static constexpr Color Magenta() { return RGB(255, 0, 255); }
    static constexpr Color Transparent() { return RGBA(0, 0, 0, 0); }
};

struct Surface {
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t PixelsPerScanLine = 0;
    std::uint32_t PixelFormatValue = static_cast<std::uint32_t>(PixelFormat::RedGreenBlueReserved8);
    volatile std::uint8_t* Pixels = nullptr;
    bool Volatile = false;
    std::shared_ptr<std::vector<std::uint8_t>> Storage;
    // Shared lifetime token lets a GraphicsContext reject a binding after its Surface is destroyed.
    std::shared_ptr<void> Lifetime;

    bool valid() const;
    std::size_t byte_size() const;
};

enum class SurfaceError {
    None,
    InvalidDimensions,
    InvalidStride,
    SizeOverflow,
    AllocationFailure,
    NullPixels,
    UnsupportedPixelFormat,
};

struct SurfaceResult {
    Surface surface;
    SurfaceError error = SurfaceError::None;
    explicit operator bool() const { return error == SurfaceError::None && surface.valid(); }
};

SurfaceResult CreateSurface(std::uint32_t width, std::uint32_t height,
                            PixelFormat format = PixelFormat::RedGreenBlueReserved8);
SurfaceResult FromFramebuffer(volatile std::uint8_t* pixels, std::uint32_t width, std::uint32_t height,
                              std::uint32_t pixels_per_scan_line, PixelFormat format);

std::optional<std::uint32_t> PackColor(Color color, PixelFormat format);
bool PutPixel(Surface& surface, std::int32_t x, std::int32_t y, Color color);
void DrawLine(Surface& surface, std::int32_t x0, std::int32_t y0,
              std::int32_t x1, std::int32_t y1, Color color);
void DrawRect(Surface& surface, std::int32_t x, std::int32_t y,
              std::int32_t width, std::int32_t height, Color color);
void FillRect(Surface& surface, std::int32_t x, std::int32_t y,
              std::int32_t width, std::int32_t height, Color color);
void DrawCircle(Surface& surface, std::int32_t cx, std::int32_t cy,
                std::int32_t radius, Color color);
void FillCircle(Surface& surface, std::int32_t cx, std::int32_t cy,
                std::int32_t radius, Color color);

struct TextMetrics { std::uint32_t width = 0; std::uint32_t height = 0; };
TextMetrics MeasureText(std::string_view text, std::uint32_t glyph_width = 6,
                        std::uint32_t line_height = 8);
void DrawText(Surface& surface, std::int32_t x, std::int32_t y,
              std::string_view text, Color color, std::uint32_t glyph_width = 6,
              std::uint32_t line_height = 8);
bool Blit(const Surface& source, Surface& destination, std::int32_t x, std::int32_t y);
bool Present(const Surface& backbuffer, Surface& framebuffer);

// Graphics-context surface binding. Binding never transfers ownership. The active stack is
// thread-local, so independent execution contexts cannot redirect one another's rendering.
bool Bind(Surface& surface);
bool PushSurface(Surface& surface);
bool PopSurface();
void Unbind();
Surface* CurrentSurface();

// Context-bound overloads. They fail harmlessly when no valid surface is bound.
bool PutPixel(std::int32_t x, std::int32_t y, Color color);
void DrawLine(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, Color color);
void DrawRect(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height, Color color);
void FillRect(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height, Color color);
void DrawCircle(std::int32_t cx, std::int32_t cy, std::int32_t radius, Color color);
void FillCircle(std::int32_t cx, std::int32_t cy, std::int32_t radius, Color color);
void DrawText(std::int32_t x, std::int32_t y, std::string_view text, Color color,
              std::uint32_t glyph_width = 6, std::uint32_t line_height = 8);
bool Blit(const Surface& source, std::int32_t x, std::int32_t y);

struct Rect { std::int32_t x = 0, y = 0, width = 0, height = 0; };
enum class HorizontalAlign { Left, Center, Right };
enum class VerticalAlign { Top, Center, Bottom };
Rect Centered(Rect parent, std::int32_t width, std::int32_t height);
Rect Align(Rect parent, std::int32_t width, std::int32_t height,
           HorizontalAlign horizontal, VerticalAlign vertical);

struct Label { Rect bounds; std::string_view text; Color color = Color::White(); };
struct Button { Rect bounds; std::string_view text; Color background = Color::RGB(40, 80, 160); Color foreground = Color::White(); };
struct Panel { Rect bounds; Color background = Color::RGB(16, 24, 40); Color border = Color::RGB(80, 120, 180); };
struct ProgressBar { Rect bounds; std::uint32_t value = 0; std::uint32_t maximum = 100; Color track = Color::RGB(32, 40, 56); Color fill = Color::RGB(40, 180, 120); };

void Render(Surface& surface, const Label& label);
void Render(Surface& surface, const Button& button);
void Render(Surface& surface, const Panel& panel);
void Render(Surface& surface, const ProgressBar& progress);

} // namespace arco::graphics
