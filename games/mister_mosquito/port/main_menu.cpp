/*
 * The main menu's PC entries: PC SETTINGS and EXIT GAME below the game's own
 * STAGE, COLOR, RECORD and FILE, and the screens they open, drawn as the
 * game's own (FINDINGS.md, "The main menu").
 *
 * The menu is the task F_0016D550, one 24 KB state machine. Each of its
 * screens shows four rows: frame objects (0x160-byte GS packets, a list at
 * $sp+0x5F0, set up each frame with 0x00192EE0 at x f12, y f13 - and drawn
 * with 0x00192B40), the rows' text (64-byte strings at $sp+0x170..0x1A0,
 * drawn with 0x0013CC40), and one highlight box ($sp+0xED0, placed with
 * 0x001918F0 at row $s4's y, drawn with 0x00191780). $s6 is the screen (0 the
 * main menu), $s4 the cursor, $s7 the help line, $sp+0x110 its second line.
 *
 * The game's cursor never goes past FILE here: the two rows below it are the
 * port's. Down on FILE moves into them, the port's own cursor taking over -
 * the game's still on FILE, its highlight drawn on the port's row instead. At
 * the menu's yield (0x0017AD38), once its frame is drawn, the two rows are
 * drawn as the game draws its own: FILE's frame copied a row and two lower,
 * the text in the game's font. The port reads Up, Down, Left, Right, Cross,
 * START, Triangle and Circle when the menu goes on, before the game sees them.
 *
 * PC SETTINGS turns the main menu into the PC settings screen: the six rows
 * the PC's settings (the engine's, PcSettings) and BACK, Left/Right or Cross
 * changing the row's, the help line saying what it is. EXIT GAME asks first,
 * as the game's own questions do: YES and NO in the middle rows (the others
 * moved off the screen), the question on the help line, NO chosen at first.
 *
 * The rows' strings and frames live in the menu text file's block for a
 * language not in use (MENU/TEXT_BIN.BIN holds all four, 0x0031BDAC), so no
 * memory the game uses is touched.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/memory.h"
#include "ps2host/run.h"

void F_00192B40(lifted::Context& c);   // draw a frame object
void F_00192EE0(lifted::Context& c);   // set a frame object up (a2 its alpha)
void F_0013C660(lifted::Context& c);   // a string's x, centred at a1 (scale f12)
void F_0013CC40(lifted::Context& c);   // draw a string at f12, f13 (scale f14, font a1)
void F_001918F0(lifted::Context& c);   // place a box object
void Copied2D(u32 to, u32 from, u32 bytes);   // widescreen.cpp: a copied packet tagged
void Piece2D(void (*draw)(lifted::Context&), lifted::Context& c);   // widescreen.cpp: a builder's call
void F_00191780(lifted::Context& c);   // draw a box object
void F_0017B4C0(lifted::Context& c);   // a menu sound
void F_001C4500(lifted::Context& c);   // the menu text section's string a0

namespace {

constexpr u32 kMenuYieldReturn = 0x0017AD40u;
constexpr u32 kTextFile = 0x0031BDACu, kLanguage = 0x0031BDA0u;
constexpr u32 kHeld = 0x0031C1E8u, kPressed = 0x0031C1ECu;
constexpr u32 kUp = 0x1000u, kRight = 0x2000u, kDown = 0x4000u, kLeft = 0x8000u;
constexpr u32 kTriangle = 0x10u, kCircle = 0x20u, kCross = 0x40u, kStart = 0x800u;
constexpr u32 kTaken = kUp | kRight | kDown | kLeft | kTriangle | kCircle | kCross | kStart;
constexpr u32 kFont = 0x0031BC30u;
constexpr u32 kRowStep = 19;                    // the rows' spacing, in field lines
constexpr u32 kFileHelp = 0x19;                 // FILE's description in the text section
constexpr u32 kYes = 230, kNo = 231;            // the text section's YES and NO
constexpr u32 kFrameSetUpReturn = 0x001788C8u;  // the menu's per-row 0x00192EE0
// Where the game draws its four rows' text from (0x0013CC40's return).
constexpr u32 kRowText[4] = {0x0017A2D4u, 0x0017A344u, 0x0017A3B4u, 0x0017A424u};
constexpr int kRows = 6;

const char* const kEntries[2] = {"PC SETTINGS", "EXIT GAME"};
const char* const kEntryHelps[2] = {"Change the PC version's settings.", "Quit the game."};

enum class Mode { Main, Settings, Confirm };

struct State {
    Mode mode = Mode::Main;
    int row = 0;                // the main menu: the port's row under the cursor (0 none, 1, 2)
    int cursor = 0;             // the PC screens' own cursor, row 0-5
    u32 sp = 0;                 // the menu's frame, this frame
    bool own_highlight = false; // drawing the highlight ourselves (the game's is skipped)
    bool main = false;          // the main menu showing (its screen 0)
    std::string yes = "YES", no = "NO";
} g;

ps2host::Memory& M() { return ps2host::GuestMemory(); }
u32 R32(u32 a) { return u32(M().Read(a, 4)); }
void W32(u32 a, u32 v) { M().Write(a, v, 4); }
u32 FloatBits(float f) { u32 v; std::memcpy(&v, &f, 4); return v; }
float BitsFloat(u32 v) { float f; std::memcpy(&f, &v, 4); return f; }
u64 Sx(u32 v) { return u64(s64(s32(v))); }
bool Guest(u32 a) { return a >= 0x00100000u && a < 0x02000000u; }

void WriteString(u32 at, const std::string& s, u32 room) {
    for (u32 i = 0; i < room; ++i) M().Write(at + i, i < s.size() && i + 1 < room ? u8(s[i]) : 0, 1);
}

std::string ReadString(u32 at, u32 room) {
    std::string s;
    for (u32 i = 0; i < room; ++i) {
        const char ch = char(M().Read(at + i, 1));
        if (!ch) break;
        s += ch;
    }
    return s;
}

// The port's guest memory: a block of the menu text file, in a language the
// game is not showing. 0 when the file is not there yet.
u32 Pool() {
    const u32 file = R32(kTextFile);
    if (!Guest(file)) return 0;
    const u32 language = R32(kLanguage);
    const u32 donor = language == 5 ? 4 : 5;            // Spanish's block, or Italian's when Spanish is on
    const u32 block = R32(file + 4 * donor);
    if (block <= file || block >= file + 0x10000u) return 0;
    return (block + 0x200u) & ~15u;
}
// The pool's layout: two frames, then a string for each of the six rows.
u32 FrameAt(u32 pool, int k) { return pool + 0x160u * u32(k); }
u32 TextAt(u32 pool, int row) { return pool + 0x2C0u + 0x40u * u32(row); }

// The PC settings the screen shows: five (a texture pack's left to Tab), then BACK.
std::vector<ps2host::PcSetting> Settings() {
    std::vector<ps2host::PcSetting> all = ps2host::PcSettings(), shown;
    for (const auto& s : all)
        if (s.label != "TEXTURES" && shown.size() < kRows - 1) shown.push_back(s);
    return shown;
}
int BackRow() { return int(Settings().size()); }

// What row `row` (0-5) says in the PC screens; empty for a hidden row.
std::string RowText(int row) {
    if (g.mode == Mode::Confirm) return row == 1 ? g.yes : row == 2 ? g.no : "";
    const auto settings = Settings();
    if (row < int(settings.size())) return settings[std::size_t(row)].label + " " + settings[std::size_t(row)].value;
    return row == int(settings.size()) ? "BACK" : "";
}

// The help line's two lines.
void SetHelp(lifted::Context& c, const std::string& first, const std::string& second) {
    const u32 line1 = u32(c.r[23]), line2 = R32(u32(c.r[29]) + 0x110);
    if (Guest(line1)) WriteString(line1, first, 0x40);
    if (Guest(line2)) WriteString(line2, second, 0x40);
}

std::string GameString(lifted::Context& c, u32 index) {
    const lifted::Context saved = c;
    c.r[4] = index;
    F_001C4500(c);
    const std::string s = ReadString(u32(c.r[2]), 0x40);
    c = saved;
    return s;
}

void ShowHelp(lifted::Context& c) {
    switch (g.mode) {
        case Mode::Main:
            if (g.row) SetHelp(c, kEntryHelps[g.row - 1], "");
            else SetHelp(c, GameString(c, kFileHelp), "");
            break;
        case Mode::Settings: {
            const auto settings = Settings();
            if (g.cursor < int(settings.size()))
                SetHelp(c, settings[std::size_t(g.cursor)].help, "Left/Right: change    Triangle: back");
            else
                SetHelp(c, "Return to the Main Menu.", "");
            break;
        }
        case Mode::Confirm:
            SetHelp(c, "Do you want to quit the game?", "");
            break;
    }
}

void Moved(lifted::Context& c) { W32(u32(c.r[28]) - 0x7A9Cu, 1); }   // the game's cursor sound

void Sound(lifted::Context& c, u32 which) {
    const lifted::Context saved = c;
    c.r[4] = which;
    F_0017B4C0(c);
    c = saved;
}

bool RowsShown(u32 sp, u32 screen) {
    return screen == 0 && R32(sp + 0x510) == 0 && R32(sp + 0x410) != 0;
}

// A row's text drawn as the game draws its own: centred on 0x21D at `scale`.
void DrawRowText(lifted::Context& c, u32 text, float y, float scale) {
    c.r[4] = text;
    c.r[5] = 0x21D;
    c.f[12] = FloatBits(scale);
    F_0013C660(c);
    const s32 x = s32(c.r[2]);
    c.r[4] = text;
    c.r[5] = kFont;
    c.f[12] = FloatBits(float(x));
    c.f[13] = FloatBits(y);
    c.f[14] = FloatBits(scale);
    F_0013CC40(c);
}

float RowScale() { return g.mode == Mode::Settings ? 0.45f : 0.55f; }

// Which row the highlight is on, -1 for the game's own.
int HighlightRow() {
    if (g.mode != Mode::Main) return g.cursor;
    return g.row ? 3 + g.row : -1;
}

void Back(lifted::Context& c, int row) {
    g.mode = Mode::Main;
    g.row = row;
    Sound(c, 4);
    ShowHelp(c);
}

}  // namespace

/* At the menu's yield, its frame drawn: the port's two rows, and the
   highlight on the port's row when the cursor is there. */
void MainMenuBeforeYield(lifted::Context& c) {
    const u32 sp = u32(c.r[29]), screen = u32(c.r[22]);
    g.sp = sp;
    g.main = screen == 0;
    if (!RowsShown(sp, screen)) return;
    const u32 pool = Pool();
    const u32 list = R32(sp + 0x5F0);
    if (!pool || !Guest(list)) return;
    const lifted::Context saved = c;
    const u32 file = list + 3 * 0x160u;                 // FILE's frame
    const u32 file_y = R32(file + 0x98);
    if (g.mode != Mode::Confirm) {
        for (int k = 0; k < 2; ++k) {
            const u32 frame = FrameAt(pool, k), down = kRowStep * u32(k + 1);
            for (u32 i = 0; i < 0x160; i += 4) W32(frame + i, R32(file + i));
            for (u32 half : {0u, 0xB0u}) {
                W32(frame + half + 0x98, file_y + down);
                W32(frame + half + 0x54, R32(file + half + 0x54) + 16 * down);
                W32(frame + half + 0x74, R32(file + half + 0x74) + 16 * down);
            }
            Copied2D(frame, file, 0x160);
            c.r[4] = frame;
            F_00192B40(c);
            const u32 text = TextAt(pool, 4 + k);
            WriteString(text, g.mode == Mode::Main ? std::string(kEntries[k]) : RowText(4 + k), 0x40);
            if (ReadString(text, 1).size()) DrawRowText(c, text, float(file_y + down + 4), RowScale());
        }
    }
    const int lit = HighlightRow();
    if (lit >= 0) {
        // The highlight on the port's row: the first row's place, as many rows lower.
        const s32 x = s32(BitsFloat(R32(sp + 0x2080)));
        const s32 y = s32(BitsFloat(R32(sp + 0x2090))) + s32(kRowStep) * lit;
        c.r[4] = sp + 0xED0;
        c.r[5] = Sx(u32(x));
        c.r[6] = Sx(u32(y));
        c.r[7] = 0;
        c.r[8] = 0x98;
        c.r[9] = 0xC;
        c.r[10] = 0x44;
        c.r[11] = sp + 0x1F10;
        Piece2D(F_001918F0, c);
        c.r[4] = sp + 0xED0;
        g.own_highlight = true;
        F_00191780(c);
        g.own_highlight = false;
    }
    c = saved;
}

/* When the menu goes on, before its frame's code: the cursor in and out of
   the port's rows and screens, and their choices. */
void MainMenuAfterYield(lifted::Context& c) {
    const u32 sp = u32(c.r[29]), screen = u32(c.r[22]), cursor = u32(c.r[20]);
    g.sp = sp;
    g.main = screen == 0;
    if (screen != 0 || ((g.row || g.mode != Mode::Main) && cursor != 3)) {
        g.row = 0;
        g.mode = Mode::Main;
        return;
    }
    const u32 pressed = R32(kPressed), held = R32(kHeld);
    if (g.mode == Mode::Main && !g.row) {
        if (cursor == 3 && (pressed & kDown) && RowsShown(sp, screen)) {
            g.row = 1;
            Moved(c);
            ShowHelp(c);
            W32(kPressed, pressed & ~kDown);
            W32(kHeld, held & ~kDown);
        }
        return;
    }
    // The port's rows and screens: the game sees none of these buttons.
    W32(kPressed, pressed & ~kTaken);
    W32(kHeld, held & ~kTaken);
    const bool up = pressed & kUp, down = pressed & kDown, left = pressed & kLeft, right = pressed & kRight;
    const bool choose = pressed & (kCross | kStart), back = pressed & (kTriangle | kCircle);
    switch (g.mode) {
        case Mode::Main: {
            const int before = g.row;
            if (up) g.row -= 1;
            else if (down && g.row < 2) g.row += 1;
            if (g.row != before) {
                Moved(c);
                ShowHelp(c);
            } else if (choose) {
                Sound(c, 4);
                g.cursor = g.row == 1 ? 0 : 2;              // EXIT GAME's question: NO first
                g.mode = g.row == 1 ? Mode::Settings : Mode::Confirm;
                if (g.mode == Mode::Confirm) {
                    g.yes = GameString(c, kYes);
                    g.no = GameString(c, kNo);
                }
                ShowHelp(c);
            }
            break;
        }
        case Mode::Settings: {
            const int last = BackRow();
            const int before = g.cursor;
            if (up && g.cursor > 0) g.cursor -= 1;
            else if (down && g.cursor < last) g.cursor += 1;
            if (g.cursor != before) {
                Moved(c);
                ShowHelp(c);
            } else if (back || (choose && g.cursor == last)) {
                Back(c, 1);
            } else if ((left || right || choose) && g.cursor < last) {
                ps2host::StepPcSetting(g.cursor, left ? -1 : 1);
                Moved(c);
            }
            break;
        }
        case Mode::Confirm: {
            const int before = g.cursor;
            if (up) g.cursor = 1;
            else if (down) g.cursor = 2;
            if (g.cursor != before) {
                Moved(c);
            } else if (back || (choose && g.cursor == 2)) {
                Back(c, 2);
            } else if (choose) {
                Sound(c, 4);
                std::fprintf(stderr, "[menu] EXIT GAME: quit\n");
                ps2host::QuitGame();
            }
            break;
        }
    }
}

namespace {

// 0x00191780 for the menu's highlight box while the cursor is the port's:
// the game's own (on FILE) is not drawn.
void DrawBox(lifted::Context& c) {
    if (HighlightRow() >= 0 && !g.own_highlight && g.sp && u32(c.r[4]) == g.sp + 0xED0) return;
    F_00191780(c);
}

// 0x00192EE0, a frame set up at x f12, y f13: in the quit question, the
// first and last rows' frames off the screen, as the game's own questions
// show two rows (theirs slid away).
void SetUpFrame(lifted::Context& c) {
    if (g.main && g.mode == Mode::Confirm && u32(c.r[31]) == kFrameSetUpReturn && g.sp) {
        const u32 list = R32(g.sp + 0x5F0);
        const u32 row = (u32(c.r[4]) - list) / 0x160u;
        if (row == 0 || row == 3) c.f[12] = FloatBits(1000.0f);
    }
    Piece2D(F_00192EE0, c);
}

// 0x0013CC40, a string drawn. In the PC screens, the game's four rows say
// the port's (or nothing). On the main menu, the PLAYTIME and Item Recovery
// Rate line four lines lower, under the port's rows as the rows are spaced
// (it sat at the sixth row's bottom). PS2HOST_MENU_LOG=1 lists the menu's
// strings as drawn.
void DrawString(lifted::Context& c) {
    const u32 from = u32(c.r[31]);
    const bool menu = g.main && g.sp && u32(c.r[29]) == g.sp;
    if (menu && g.mode != Mode::Main) {
        for (int row = 0; row < 4; ++row) {
            if (from != kRowText[row]) continue;
            const std::string text = RowText(row);
            const u32 pool = Pool();
            if (text.empty() || !pool) return;
            const u32 at = TextAt(pool, row);
            WriteString(at, text, 0x40);
            const float y = BitsFloat(u32(c.f[13]));
            const lifted::Context saved = c;
            DrawRowText(c, at, y, RowScale());
            c = saved;
            return;
        }
    }
    if (menu && (from == 0x0017A00Cu || from == 0x0017A038u))
        c.f[13] = FloatBits(BitsFloat(u32(c.f[13])) + 4.0f);
    static const bool log = std::getenv("PS2HOST_MENU_LOG") != nullptr;
    static int logged = 0;
    if (log && logged < 400 && menu) {
        ++logged;
        std::fprintf(stderr, "[menu] string from 0x%08X y %.1f \"%s\"\n", from, double(BitsFloat(u32(c.f[13]))),
                     ReadString(u32(c.r[4]), 40).c_str());
    }
    F_0013CC40(c);
}

}  // namespace

void RegisterMainMenu() {
    ps2host::Override(0x0013CC40u, DrawString, "a string (the main menu's PC rows and screens)");
    ps2host::Override(0x00191780u, DrawBox, "a box object (the main menu's PC rows)");
    ps2host::Override(0x00192EE0u, SetUpFrame, "a frame set up (the quit question's two rows)");
}

bool IsMainMenuYield(const lifted::Context& c) { return u32(c.r[31]) == kMenuYieldReturn; }
