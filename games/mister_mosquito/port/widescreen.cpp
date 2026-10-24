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
 * shape.
 *
 * The 2D - the HUD, the menus - keeps its 4:3 shape, each element brought in
 * towards the middle of the screen, or towards its left or right edge (an
 * anchor), as Burnout 3's port does it. Anchors are settings
 * ("hud.<context>.<object>.<cell>" = left, right, centre or stretch), set
 * with the HUD editor (F10) and saved as they change; kDefaultAnchors gives
 * the port's own (FINDINGS.md, "A wider picture").
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/hud.h"
#include "ps2host/memory.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

void F_001B9640(lifted::Context& c);   // the camera's view-screen matrix
void F_00101E50(lifted::Context& c);   // a vector to GS fixed point (vftoi4): the 2D's vertices
void F_0013CCC0(lifted::Context& c);   // a string
// The 2D's builders (kBuilders) and the screen's effects (kEffects).
void F_00190690(lifted::Context& c);
void F_00190EE0(lifted::Context& c);
void F_00191200(lifted::Context& c);
void F_00191480(lifted::Context& c);
void F_001918F0(lifted::Context& c);
void F_00191B80(lifted::Context& c);
void F_00191E60(lifted::Context& c);
void F_00192C30(lifted::Context& c);
void F_00192D50(lifted::Context& c);
void F_00192EE0(lifted::Context& c);
void F_001932A0(lifted::Context& c);
void F_00135490(lifted::Context& c);
void F_00135B60(lifted::Context& c);
void F_00135F00(lifted::Context& c);
void F_00136350(lifted::Context& c);

namespace {

using ps2host::hud::Anchor;
using ps2host::hud::AnchorName;
using ps2host::hud::AnchorOf;
using ps2host::hud::CellOf;
using ps2host::hud::ElementId;
using ps2host::hud::Hidden;
using ps2host::hud::Place;
using ps2host::hud::Seen;
using ps2host::hud::Squeeze;
using ps2host::hud::Wider;

u32 FloatBits(float f) { u32 v; std::memcpy(&v, &f, 4); return v; }
float BitsFloat(u32 v) { float f; std::memcpy(&f, &v, 4); return f; }

/* 0x001B9640: the view-screen matrix, its x aspect (f13) narrowed. */
void ViewScreen(lifted::Context& c) {
    const float wider = Wider();
    if (wider > 1.001f || wider < 0.999f) c.f[13] = FloatBits(BitsFloat(u32(c.f[13])) / wider);
    F_001B9640(c);
}

u32 GameFrame() { return u32(ps2host::GuestMemory().Read(0x002D4E80u, 4)); }

// ------------------------------------------------------------ elements and anchors
//
// An element (ps2host/hud.h) is what one piece of the game's code draws in
// one part of the screen: the object (the function whose call drew it), what
// called that (its context: a menu, the stage's HUD), and the cell of a 3x3
// grid over the 4:3 picture its middle lies in. The same in every run. The
// anchors and the HUD editor (F10) are the engine's.

/* The port's own anchors (the HUD editor's settings take precedence); a
   key ending in '.' is the object's in every cell. Context 0: a task's own
   function (tasks.cpp), the first call of its stack. */
const std::pair<const char*, Anchor> kDefaultAnchors[] = {
    // In a stage (0x0012EE40): the health hearts top left, the stress orb
    // and the radar bottom left, the blood count bottom right.
    {"hud.000000.12EE40.TL", Anchor::kLeft},
    {"hud.000000.12EE40.BL", Anchor::kLeft},
    {"hud.000000.12EE40.BR", Anchor::kRight},
    // The markers on the targets in the room: placed on the 3D, which the
    // camera made wider already.
    {"hud.000000.145C70.", Anchor::kStretch},
    {"hud.145C70.146A00.", Anchor::kStretch},
};

// The game's 2D drawing (FINDINGS.md, "A wider picture"). 0x00101E50
// (vftoi4: x and y to the GS's fixed point) makes every vertex of it, called
// only by the builders below, each writing one shape's packet: a box
// (0x001918F0), a frame (0x00192EE0), sprites and strips. A string is
// 0x0013CCC0, its glyphs one builder call each. The screen's effects
// (0x00135490, 0x00135B60, 0x00135F00, 0x00136350: copies of the whole
// picture, and into smaller pictures) are the 3D's, never placed.
constexpr u32 kBuilders[] = {0x00190690, 0x00190EE0, 0x00191200, 0x00191480, 0x001918F0, 0x00191B80,
                             0x00191E60, 0x00192C30, 0x00192D50, 0x00192EE0, 0x001932A0};
constexpr u32 kString = 0x0013CCC0;
constexpr u32 kEffects[] = {0x00135490, 0x00135B60, 0x00135F00, 0x00136350};

// What draws for an object, not the object: the builders, the string and
// what calls them for a string (0x0013C9D0, 0x0013CA90, 0x0013CB40,
// 0x0013CC00, 0x0013CC40), a frame (0x00192DE0, 0x00192B40) or a box
// (0x00191800, 0x00191780); a task's yield (0x0011AF7C, the port's menu
// rows drawn there).
bool Library(u32 f) {
    for (u32 b : kBuilders)
        if (f == b) return true;
    switch (f) {
        case 0x00101E50: case kString: case 0x0013C9D0: case 0x0013CA90: case 0x0013CB40: case 0x0013CC00:
        case 0x0013CC40: case 0x00192DE0: case 0x00192B40: case 0x00191800: case 0x00191780: case 0x0011AF7C:
            return true;
        default:
            return false;
    }
}

/* Who drew: the first function out that is not the library's, and what
   called it. */
void Caller(ElementId* id) {
    const std::vector<u32>& calls = ps2host::CurrentCalls();
    std::size_t i = calls.size();
    while (i > 0 && Library(calls[i - 1])) --i;
    id->object = i >= 1 ? calls[i - 1] : 0;
    id->context = i >= 2 ? calls[i - 2] : 0;
}

// ------------------------------------------------------------ the 2D's vertices
//
// A piece is a builder's call, or a string's: the shape or the words one
// object draws. Each vertex is tagged as it is made with its piece, whose
// extent gives the element's cell. When the packet goes to the GS
// (SetPath2Filter, a copy), each tagged vertex still as written is placed by
// its piece's element. A packet the game keeps from frame to frame keeps its
// tags; one written over, or copied (the port's menu rows: Copied2D), is
// tagged again.

struct Piece {
    ElementId id;                       // its cell found when first placed
    int lo_x = 0x10000, hi_x = -1, lo_y = 0x10000, hi_y = -1;
    bool placed = false;
    Anchor anchor = Anchor::kCentre;
    bool across = false;                // the picture's whole width: its vertices at the edges stay
    void Add(int x, int y) {
        lo_x = std::min(lo_x, x);
        hi_x = std::max(hi_x, x);
        lo_y = std::min(lo_y, y);
        hi_y = std::max(hi_y, y);
    }
};

struct Tag {
    u32 x;                              // the x word as written
    std::shared_ptr<Piece> piece;
};

std::unordered_map<u32, Tag> g_tags;     // by the vertex's quadword

// The piece being drawn: the outermost builder's or string's call.
int g_depth = 0;
std::shared_ptr<Piece> g_piece;
u32 g_frame = ~0u;

constexpr int kMiddle = 0x8000;          // the picture's middle across, in GS units
constexpr int kTop = 0x7900;             // its top (the game's XYOFFSET y: 1936)
constexpr int kHalf = 320 * 16;          // half the picture's width

/* A builder or a string: the outermost call a piece. */
void DrawPiece(void (*draw)(lifted::Context&), lifted::Context& c) {
    if (g_depth++ == 0) {
        g_piece = std::make_shared<Piece>();
        Caller(&g_piece->id);
        const u32 frame = GameFrame();
        if (frame != g_frame) ps2host::hud::EditorInput();
        g_frame = frame;
    }
    draw(c);
    if (--g_depth == 0) g_piece.reset();
}

template <void (*Draw)(lifted::Context&)>
void PieceOf(lifted::Context& c) { DrawPiece(Draw, c); }

/* A screen effect's builder: its vertices not tagged. */
template <void (*Draw)(lifted::Context&)>
void Effect(lifted::Context& c) {
    const int depth = g_depth;
    const std::shared_ptr<Piece> piece = g_piece;
    g_piece.reset();
    g_depth = 1;
    Draw(c);
    g_depth = depth;
    g_piece = piece;
}

/* 0x00101E50(destination, &vector): a vertex, tagged with its piece. */
void Vertex(lifted::Context& c) {
    const u32 at = u32(c.r[4]) & ~15u;
    F_00101E50(c);
    if (!g_piece) {
        g_tags.erase(at);
        return;
    }
    const u32 x = generated::ReadU32(at), y = generated::ReadU32(at + 4);
    if (x < 0x4000 || x > 0xC000 || y < 0x4000 || y > 0xC000) {     // not a place on the screen
        g_tags.erase(at);
        return;
    }
    g_piece->Add(int(x), int(y));
    g_tags[at] = {x, g_piece};
}

// ------------------------------------------------------------ to the GS

/* A piece's element and anchor, found the first time it goes to the GS:
   its cell from the middle of its extent. */
void PlacePiece(Piece& p) {
    if (p.placed) return;
    p.placed = true;
    const float cx = float((p.lo_x + p.hi_x) / 2 - kMiddle) / 16.0f + 320.0f;
    const float cy = float((p.lo_y + p.hi_y) / 2 - kTop) / 16.0f * 2.0f;   // drawn a field (224 lines) at a time
    CellOf(cx, cy, p.id.cell);
    p.across = p.lo_x <= kMiddle - kHalf + 16 * 8 && p.hi_x >= kMiddle + kHalf - 16 * 8;
    p.anchor = AnchorOf(p.id);
    // PS2HOST_HUD_LOG=1: each element as it is first drawn.
    static const bool log = std::getenv("PS2HOST_HUD_LOG") != nullptr;
    static std::map<std::string, int> logged;
    if (log && !logged[p.id.Key()]++)
        std::fprintf(stderr, "[hud] f%u %s (%d,%d)-(%d,%d)%s %s\n", GameFrame(), p.id.Key().c_str(),
                     (p.lo_x - kMiddle) / 16 + 320, (p.lo_y - kTop) / 16, (p.hi_x - kMiddle) / 16 + 320,
                     (p.hi_y - kTop) / 16, p.across ? " across" : "", AnchorName(p.anchor));
}

/* A block of PATH2 on its way: each tagged vertex, still as written, placed. */
void Path2(u32* words, u32 count, u32 from) {
    const float k = Squeeze();
    if (k >= 1.0f && !ps2host::hud::EditorOn()) return;
    for (u32 i = (16 - (from & 15)) % 16 / 4; i + 4 <= count; i += 4) {
        auto found = g_tags.find(from + 4 * i);
        if (found == g_tags.end() || found->second.x != words[i]) continue;
        Piece& p = *found->second.piece;
        PlacePiece(p);
        Seen(p.id);
        if (Hidden(p.id)) {
            words[i] = u32(p.lo_x);
            continue;
        }
        if (k >= 1.0f) continue;
        const int x = int(words[i]);
        if (p.across && (x <= kMiddle - kHalf || x >= kMiddle + kHalf)) continue;
        const float placed = Place(float(x), float(kMiddle), float(kHalf), k, p.anchor);
        words[i] = u32(std::lround(placed));
    }
}

}  // namespace

/* A builder's call from the port's own code (main_menu.cpp): a piece, as
   the game's own calls are. */
void Piece2D(void (*draw)(lifted::Context&), lifted::Context& c) { DrawPiece(draw, c); }

/* The port's own copy of a packet (the menu's rows: main_menu.cpp), its
   vertices tagged as the original's, each piece its own. */
void Copied2D(u32 to, u32 from, u32 bytes) {
    std::map<Piece*, std::shared_ptr<Piece>> pieces;
    for (u32 o = 0; o < bytes; o += 16) {
        auto found = g_tags.find(from + o);
        if (found == g_tags.end()) continue;
        auto& piece = pieces[found->second.piece.get()];
        if (!piece) {
            piece = std::make_shared<Piece>();
            piece->id = found->second.piece->id;
        }
        const u32 x = generated::ReadU32(to + o), y = generated::ReadU32(to + o + 4);
        piece->Add(int(x), int(y));
        g_tags[to + o] = {x, piece};
    }
}

void RegisterWidescreen() {
    ps2host::hud::Game game;
    game.defaults = kDefaultAnchors;
    game.default_count = std::size(kDefaultAnchors);
    ps2host::hud::SetGame(game);
    ps2host::Override(0x001B9640u, ViewScreen, "the camera's projection, wider for a wider picture");
    ps2host::Override(0x00101E50u, Vertex, "the 2D's vertices: tagged with what drew them (the HUD in a wider picture)");
    const char* const piece = "a piece of the 2D (the HUD in a wider picture)";
    ps2host::Override(kString, PieceOf<F_0013CCC0>, piece);
    ps2host::Override(0x00190690u, PieceOf<F_00190690>, piece);
    ps2host::Override(0x00190EE0u, PieceOf<F_00190EE0>, piece);
    ps2host::Override(0x00191200u, PieceOf<F_00191200>, piece);
    ps2host::Override(0x00191480u, PieceOf<F_00191480>, piece);
    ps2host::Override(0x001918F0u, PieceOf<F_001918F0>, piece);
    ps2host::Override(0x00191B80u, PieceOf<F_00191B80>, piece);
    ps2host::Override(0x00191E60u, PieceOf<F_00191E60>, piece);
    ps2host::Override(0x00192C30u, PieceOf<F_00192C30>, piece);
    ps2host::Override(0x00192D50u, PieceOf<F_00192D50>, piece);
    // 0x00192EE0's override is the main menu's (main_menu.cpp), a piece through Piece2D.
    ps2host::Override(0x001932A0u, PieceOf<F_001932A0>, piece);
    const char* const effect = "a screen effect: the 3D's, not placed with the HUD";
    ps2host::Override(0x00135490u, Effect<F_00135490>, effect);
    ps2host::Override(0x00135B60u, Effect<F_00135B60>, effect);
    ps2host::Override(0x00135F00u, Effect<F_00135F00>, effect);
    ps2host::Override(0x00136350u, Effect<F_00136350>, effect);
    ps2host::SetPath2Filter(Path2);
}
