/*
 * Mister Mosquito in a wider picture (ASPECT in PC SETTINGS).
 *
 * The game draws for a 4:3 television. Its 3D is made wider where the camera
 * makes its projection: 0x001B9640, a view-screen matrix as libvu0's
 * sceVu0ViewScreenMatrix makes one (f12 the screen's distance, 320 over the
 * tangent of half the field of view; f13 and f14 the x and y aspect, 1.0 and
 * 0.47; f15, f16 the centre, 2048). For a picture `wider` times as wide as
 * 4:3, x is drawn that much narrower: more of the scene at the sides, the same
 * up and down, nothing stretched when the display shows the picture that
 * shape. The 2D - the HUD, the menus - keeps its 4:3 shape (FINDINGS.md).
 */

#include <cstring>

#include "ps2host/host.h"
#include "ps2host/run.h"

void F_001B9640(lifted::Context& c);   // the camera's view-screen matrix

namespace {

u32 FloatBits(float f) { u32 v; std::memcpy(&v, &f, 4); return v; }
float BitsFloat(u32 v) { float f; std::memcpy(&f, &v, 4); return f; }

// How much wider than 4:3 the picture is shown.
float Wider() { return ps2host::PictureAspect() / (4.0f / 3.0f); }

/* 0x001B9640: the view-screen matrix, its x aspect (f13) narrowed. */
void ViewScreen(lifted::Context& c) {
    const float wider = Wider();
    if (wider > 1.001f || wider < 0.999f) c.f[13] = FloatBits(BitsFloat(u32(c.f[13])) / wider);
    F_001B9640(c);
}

}  // namespace

void RegisterWidescreen() {
    ps2host::Override(0x001B9640u, ViewScreen, "the camera's projection, wider for a wider picture");
}
