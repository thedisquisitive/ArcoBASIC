#include "arco/graphics.hpp"

#include <fstream>

void render_arcology_ui(arco::graphics::Surface& backbuffer);

int main() {
    using namespace arco::graphics;
    auto result = CreateSurface(1280, 800, PixelFormat::RedGreenBlueReserved8);
    if (!result) return 1;
    render_arcology_ui(result.surface);
    std::ofstream output("arcology-ui.ppm", std::ios::binary);
    output << "P6\n" << result.surface.Width << ' ' << result.surface.Height << "\n255\n";
    for (std::uint32_t y = 0; y < result.surface.Height; ++y) {
        for (std::uint32_t x = 0; x < result.surface.Width; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * result.surface.PixelsPerScanLine + x) * 4;
            const auto r = result.surface.Pixels[offset + 0];
            const auto g = result.surface.Pixels[offset + 1];
            const auto b = result.surface.Pixels[offset + 2];
            output.put(static_cast<char>(r)); output.put(static_cast<char>(g)); output.put(static_cast<char>(b));
        }
    }
    return output ? 0 : 1;
}
