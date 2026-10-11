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
#include <chrono>
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

u32 FloatBits(float f) { u32 v; std::memcpy(&v, &f, 4); return v; }
float BitsFloat(u32 v) { float f; std::memcpy(&f, &v, 4); return f; }

// How much wider than 4:3 the picture is shown.
float Wider() { return ps2host::PictureAspect() / (4.0f / 3.0f); }

/* 4:3 over the picture's shape: 1 for a 4:3 picture. */
float Squeeze() { return std::min(1.0f, 1.0f / Wider()); }

/* 0x001B9640: the view-screen matrix, its x aspect (f13) narrowed. */
void ViewScreen(lifted::Context& c) {
    const float wider = Wider();
    if (wider > 1.001f || wider < 0.999f) c.f[13] = FloatBits(BitsFloat(u32(c.f[13])) / wider);
    F_001B9640(c);
}

u32 GameFrame() { return u32(ps2host::GuestMemory().Read(0x002D4E80u, 4)); }

// ------------------------------------------------------------ elements and anchors
//
// An element is what one piece of the game's code draws in one part of the
// screen: the object (the function whose call drew it), what called that
// (its context: a menu, the stage's HUD), and the cell of a 3x3 grid over
// the 4:3 picture its middle lies in. The same in every run.

enum class Anchor { kCentre, kLeft, kRight, kStretch };

struct ElementId {
    u32 context = 0, object = 0;
    char cell[3] = {'?', '?', 0};        // row T M B, column L C R
    bool operator<(const ElementId& o) const {
        // Rows top to bottom, each left to right.
        if (cell[0] != o.cell[0]) return std::string("TMB").find(cell[0]) < std::string("TMB").find(o.cell[0]);
        if (cell[1] != o.cell[1]) return std::string("LCR").find(cell[1]) < std::string("LCR").find(o.cell[1]);
        if (object != o.object) return object < o.object;
        return context < o.context;
    }
    bool operator==(const ElementId& o) const {
        return context == o.context && object == o.object && cell[0] == o.cell[0] && cell[1] == o.cell[1];
    }
    bool known() const { return object != 0; }
    std::string Key() const {
        char key[48];
        std::snprintf(key, sizeof key, "hud.%06X.%06X.%s", unsigned(context), unsigned(object), cell);
        return key;
    }
};

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

const char* AnchorName(Anchor a) {
    switch (a) {
        case Anchor::kLeft: return "left";
        case Anchor::kRight: return "right";
        case Anchor::kStretch: return "stretch";
        default: return "centre";
    }
}

/* An element's anchor: its setting, else the port's default, else centred;
   remembered until the editor changes one. */
std::map<std::string, Anchor> g_anchors;
Anchor AnchorOf(const ElementId& id) {
    if (!id.known()) return Anchor::kCentre;
    const std::string key = id.Key();
    auto found = g_anchors.find(key);
    if (found != g_anchors.end()) return found->second;
    Anchor anchor = Anchor::kCentre;
    for (const auto& [name, value] : kDefaultAnchors) {
        const std::string n = name;
        if (key == n || (n.back() == '.' && key.compare(0, n.size(), n) == 0)) anchor = value;
    }
    const std::string set = ps2host::Setting(key, "");
    if (set == "left") anchor = Anchor::kLeft;
    else if (set == "right") anchor = Anchor::kRight;
    else if (set == "centre") anchor = Anchor::kCentre;
    else if (set == "stretch") anchor = Anchor::kStretch;
    g_anchors[key] = anchor;
    return anchor;
}

/* x (GS units, about `centre`, `half` its distance to the picture's edges)
   brought in by k towards the middle, or the edge it is anchored to;
   stretched, as the game drew it - across the wider picture. */
float Place(float x, float centre, float half, float k, Anchor anchor) {
    if (anchor == Anchor::kStretch) return x;
    const float a = anchor == Anchor::kLeft ? centre - half : anchor == Anchor::kRight ? centre + half : centre;
    return a + (x - a) * k;
}

/* The cell of a point of the 4:3 picture, (0, 0) its top left, 640 x 448. */
void CellOf(float x, float y, char cell[3]) {
    cell[0] = y < 448.0f / 3 ? 'T' : y < 896.0f / 3 ? 'M' : 'B';
    cell[1] = x < 640.0f / 3 ? 'L' : x < 1280.0f / 3 ? 'C' : 'R';
    cell[2] = 0;
}

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

void EditorInput();

/* A builder or a string: the outermost call a piece. */
void DrawPiece(void (*draw)(lifted::Context&), lifted::Context& c) {
    if (g_depth++ == 0) {
        g_piece = std::make_shared<Piece>();
        Caller(&g_piece->id);
        const u32 frame = GameFrame();
        if (frame != g_frame) EditorInput();
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

// ------------------------------------------------------------ the HUD editor
//
// F10 in a wider picture: a panel over the picture lists the elements on
// screen and their anchors; Page Up and Page Down choose one (it blinks),
// Home or numpad 4 anchors it left, Insert or numpad 5 in the middle, End or
// numpad 6 right, Delete or numpad 8 stretches it across the picture -
// saved at once.

struct Editor {
    bool on = false;
    ElementId chosen;
    std::map<ElementId, std::chrono::steady_clock::time_point> seen;   // drawn lately
    bool keys[7] = {};
    std::size_t shown = ~std::size_t(0);   // how many the panel lists
};
Editor g_editor;

const char* CellName(const char cell[3]) {
    static const char* const kRows[] = {"top", "middle", "bottom"};
    static const char* const kColumns[] = {"left", "centre", "right"};
    static char name[24];
    const std::size_t row = std::string("TMB").find(cell[0]), column = std::string("LCR").find(cell[1]);
    std::snprintf(name, sizeof name, "%s %s", row < 3 ? kRows[row] : "?", column < 3 ? kColumns[column] : "?");
    return name;
}

/* An element was drawn (the editor lists what is on screen). */
void Seen(const ElementId& id) {
    if (g_editor.on && id.known()) g_editor.seen[id] = std::chrono::steady_clock::now();
}

/* Whether it is hidden now: the chosen one blinks. */
bool Hidden(const ElementId& id) {
    if (!g_editor.on || !(id == g_editor.chosen)) return false;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
    return (ms / 250) % 2 == 1;
}

/* The panel: what is on screen, the one chosen, the keys. */
void ShowStatus() {
    if (!g_editor.on) {
        ps2host::SetWindowOverlay({});
        ps2host::SetWindowStatus("");
        return;
    }
    std::vector<std::string> lines = {"HUD EDITOR  (F10 closes)",
                                      "PgUp/PgDn choose   Home left   Insert centre   End right   Delete stretch", ""};
    std::size_t index = 0, count = 0;
    for (const auto& [id, when] : g_editor.seen) {
        ++count;
        const bool chosen = id == g_editor.chosen;
        if (chosen) index = count;
        char text[160];
        std::snprintf(text, sizeof text, "%s %-13s %06X/%06X   %s", chosen ? ">" : " ", CellName(id.cell),
                      unsigned(id.object), unsigned(id.context), AnchorName(AnchorOf(id)));
        lines.push_back(text);
    }
    if (!count) lines.push_back("  (no HUD drawn)");
    g_editor.shown = count;
    char title[96];
    std::snprintf(title, sizeof title, "HUD EDIT %zu/%zu", index, count);
    ps2host::SetWindowStatus(title);
    ps2host::SetWindowOverlay(lines);
}

/* A key's press (down now, up before). */
bool Press(int slot, int vk, int vk2 = 0) {
    const bool down = ps2host::WindowFocused() && (ps2host::HostKeyDown(vk) || (vk2 && ps2host::HostKeyDown(vk2)));
    const bool pressed = down && !g_editor.keys[slot];
    g_editor.keys[slot] = down;
    return pressed;
}

/* The editor's keys, read once a frame. */
void EditorInput() {
    constexpr int kF10 = 0x79, kPageUp = 0x21, kPageDown = 0x22, kHome = 0x24, kEnd = 0x23, kInsert = 0x2D;
    constexpr int kPad4 = 0x64, kPad5 = 0x65, kPad6 = 0x66, kDelete = 0x2E, kPad8 = 0x68;
    if (Press(0, kF10)) {
        g_editor.on = !g_editor.on && Squeeze() < 1.0f;
        g_editor.seen.clear();
        g_editor.chosen = ElementId{};
        g_editor.shown = ~std::size_t(0);
        std::fprintf(stderr, "[hud] editor %s\n", g_editor.on ? "on" : Squeeze() < 1.0f ? "off" : "off (a 4:3 picture)");
        ShowStatus();
    }
    if (!g_editor.on) return;
    // What has not been drawn for a while is off the screen; the panel
    // follows what is.
    const auto now = std::chrono::steady_clock::now();
    for (auto it = g_editor.seen.begin(); it != g_editor.seen.end();)
        it = now - it->second > std::chrono::milliseconds(500) ? g_editor.seen.erase(it) : std::next(it);
    if (g_editor.seen.size() != g_editor.shown) ShowStatus();
    std::vector<ElementId> list;
    for (const auto& [id, when] : g_editor.seen) list.push_back(id);
    const bool next = Press(1, kPageDown), previous = Press(2, kPageUp);
    if ((next || previous) && !list.empty()) {
        std::size_t at = list.size();
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i] == g_editor.chosen) at = i;
        if (at == list.size()) at = next ? list.size() - 1 : 0;
        g_editor.chosen = list[(at + (next ? 1 : list.size() - 1)) % list.size()];
        ShowStatus();
    }
    Anchor set = Anchor::kCentre;
    bool setting = false;
    if (Press(3, kHome, kPad4)) setting = true, set = Anchor::kLeft;
    if (Press(4, kInsert, kPad5)) setting = true, set = Anchor::kCentre;
    if (Press(5, kEnd, kPad6)) setting = true, set = Anchor::kRight;
    if (Press(6, kDelete, kPad8)) setting = true, set = Anchor::kStretch;
    if (setting && g_editor.chosen.known()) {
        const std::string key = g_editor.chosen.Key();
        ps2host::SetSetting(key, AnchorName(set));
        g_anchors[key] = set;
        std::fprintf(stderr, "[hud] %s anchored %s\n", key.c_str(), AnchorName(set));
        ShowStatus();
    }
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
    if (k >= 1.0f && !g_editor.on) return;
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
