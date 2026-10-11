/*
 * The main menu's PC entries: PC SETTINGS and EXIT GAME below the game's own
 * STAGE, COLOR, RECORD and FILE (FINDINGS.md, "The main menu").
 *
 * The menu is the task F_0016D550, one 24 KB state machine. Each of its
 * screens shows four rows: frame objects (0x160-byte GS packets, a list at
 * $sp+0x5F0, drawn with 0x00192B40), the rows' text (64-byte strings at
 * $sp+0x170..0x1A0, drawn with 0x0013CC40), and one highlight box ($sp+0xED0,
 * placed with 0x001918F0 at row $s4's y, drawn with 0x00191780). $s6 is the
 * screen (0 the main menu), $s4 the cursor, $sp+0x740 the rows (4).
 *
 * The game's cursor never goes past FILE here: the two rows below it are the
 * port's. Down on FILE moves into them, the port's own cursor taking over -
 * the game's still on FILE, its highlight drawn on the port's row instead. At
 * the menu's yield (0x0017AD38), once its frame is drawn, the two rows are
 * drawn as the game draws its own: FILE's frame copied a row and two lower,
 * the text with the game's font. Cross or START on them, taken before the
 * game sees it: PC SETTINGS opens the PC settings panel, EXIT GAME ends the
 * run as closing the window does.
 *
 * The rows' strings and frames live in the menu text file's block for a
 * language not in use (MENU/TEXT_BIN.BIN holds all four, 0x0031BDAC), so no
 * memory the game uses is touched.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "ps2host/host.h"
#include "ps2host/memory.h"
#include "ps2host/run.h"

void F_00192B40(lifted::Context& c);   // draw a frame object
void F_0013C660(lifted::Context& c);   // a string's x, centred at a1 (scale f12)
void F_0013CC40(lifted::Context& c);   // draw a string at f12, f13 (scale f14, font a1)
void F_001918F0(lifted::Context& c);   // place a box object
void F_00191780(lifted::Context& c);   // draw a box object
void F_0017B4C0(lifted::Context& c);   // a menu sound
void F_001C4500(lifted::Context& c);   // the menu text section's string a0

namespace {

constexpr u32 kMenuYieldReturn = 0x0017AD40u;
constexpr u32 kTextFile = 0x0031BDACu, kLanguage = 0x0031BDA0u;
constexpr u32 kHeld = 0x0031C1E8u, kPressed = 0x0031C1ECu;
constexpr u32 kUp = 0x1000u, kDown = 0x4000u, kCross = 0x40u, kStart = 0x800u;
constexpr u32 kFont = 0x0031BC30u;
constexpr u32 kRowStep = 19;                    // the rows' spacing, in field lines
constexpr u32 kFileHelp = 0x19;                 // FILE's description in the text section

const char* const kRows[2] = {"PC SETTINGS", "EXIT GAME"};
const char* const kHelps[2] = {"Change the PC version's settings.", "Quit the game."};

struct State {
    int row = 0;                // the port's row under the cursor: 0 none, 1 PC SETTINGS, 2 EXIT GAME
    u32 sp = 0;                 // the menu's frame, this frame
    bool own_highlight = false; // drawing the highlight ourselves (the game's is skipped)
    bool main = false;          // the main menu showing (its screen 0)
} g;

ps2host::Memory& M() { return ps2host::GuestMemory(); }
u32 R32(u32 a) { return u32(M().Read(a, 4)); }
void W32(u32 a, u32 v) { M().Write(a, v, 4); }
u32 FloatBits(float f) { u32 v; std::memcpy(&v, &f, 4); return v; }
float BitsFloat(u32 v) { float f; std::memcpy(&f, &v, 4); return f; }
u64 Sx(u32 v) { return u64(s64(s32(v))); }

void WriteString(u32 at, const std::string& s, u32 room) {
    for (u32 i = 0; i < room; ++i) M().Write(at + i, i < s.size() ? u8(s[i]) : 0, 1);
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
    if (file < 0x00100000u || file >= 0x02000000u) return 0;
    const u32 language = R32(kLanguage);
    const u32 donor = language == 5 ? 4 : 5;            // Spanish's block, or Italian's when Spanish is on
    const u32 block = R32(file + 4 * donor);
    if (block <= file || block >= file + 0x10000u) return 0;
    return (block + 0x200u) & ~15u;
}
// The pool's layout.
u32 FrameAt(u32 pool, int k) { return pool + 0x160u * u32(k); }
u32 TextAt(u32 pool, int k) { return pool + 0x2C0u + 0x40u * u32(k); }
u32 HelpAt(u32 pool, int k) { return pool + 0x340u + 0x80u * u32(k); }

bool RowsShown(u32 sp, u32 screen) {
    return screen == 0 && R32(sp + 0x510) == 0 && R32(sp + 0x410) != 0;
}

// The help line under the menu: the port's row's, or FILE's again.
void SetHelp(lifted::Context& c, int row) {
    const u32 buffer = u32(c.r[23]);                 // $s7: the help line's string
    if (buffer < 0x00100000u || buffer >= 0x02000000u) return;
    const u32 pool = Pool();
    if (!pool) return;
    const lifted::Context saved = c;
    std::string text;
    if (row) {
        text = kHelps[row - 1];
    } else {
        c.r[4] = kFileHelp;
        F_001C4500(c);
        text = ReadString(u32(c.r[2]), 0x40);
    }
    c = saved;
    WriteString(buffer, text, 0x40);
}

void Moved(lifted::Context& c) { W32(u32(c.r[28]) - 0x7A9Cu, 1); }   // the game's cursor sound

void Sound(lifted::Context& c, u32 which) {
    const lifted::Context saved = c;
    c.r[4] = which;
    F_0017B4C0(c);
    c = saved;
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
    if (!pool || list < 0x00100000u || list >= 0x02000000u) return;
    const lifted::Context saved = c;
    const u32 file = list + 3 * 0x160u;                 // FILE's frame
    const u32 file_y = R32(file + 0x98);
    for (int k = 0; k < 2; ++k) {
        const u32 frame = FrameAt(pool, k), down = kRowStep * u32(k + 1);
        for (u32 i = 0; i < 0x160; i += 4) W32(frame + i, R32(file + i));
        for (u32 half : {0u, 0xB0u}) {
            W32(frame + half + 0x98, file_y + down);
            W32(frame + half + 0x54, R32(file + half + 0x54) + 16 * down);
            W32(frame + half + 0x74, R32(file + half + 0x74) + 16 * down);
        }
        c.r[4] = frame;
        F_00192B40(c);
        // The text as the game draws its rows': centred on 0x21D, scale 0.55,
        // four lines below the frame's top.
        const u32 text = TextAt(pool, k);
        WriteString(text, kRows[k], 0x40);
        c.r[4] = text;
        c.r[5] = 0x21D;
        c.f[12] = FloatBits(0.55f);
        F_0013C660(c);
        const s32 x = s32(c.r[2]);
        c.r[4] = text;
        c.r[5] = kFont;
        c.f[12] = FloatBits(float(x));
        c.f[13] = FloatBits(float(file_y + down + 4));
        c.f[14] = FloatBits(0.55f);
        F_0013CC40(c);
    }
    if (g.row) {
        // The highlight on the port's row: FILE's place, a row or two lower.
        const s32 x = s32(BitsFloat(R32(sp + 0x2080)));
        const s32 y = s32(BitsFloat(R32(sp + 0x2090 + 12))) + s32(kRowStep) * g.row;
        c.r[4] = sp + 0xED0;
        c.r[5] = Sx(u32(x));
        c.r[6] = Sx(u32(y));
        c.r[7] = 0;
        c.r[8] = 0x98;
        c.r[9] = 0xC;
        c.r[10] = 0x44;
        c.r[11] = sp + 0x1F10;
        F_001918F0(c);
        c.r[4] = sp + 0xED0;
        g.own_highlight = true;
        F_00191780(c);
        g.own_highlight = false;
    }
    c = saved;
}

/* When the menu goes on, before its frame's code: the cursor in and out of
   the port's rows, and their choice. */
void MainMenuAfterYield(lifted::Context& c) {
    const u32 sp = u32(c.r[29]), screen = u32(c.r[22]), cursor = u32(c.r[20]);
    g.sp = sp;
    g.main = screen == 0;
    if (screen != 0 || (g.row && cursor != 3)) {
        g.row = 0;
        return;
    }
    if (ps2host::PcMenuOpen()) return;
    const u32 pressed = R32(kPressed), held = R32(kHeld);
    if (!g.row) {
        if (cursor == 3 && (pressed & kDown) && RowsShown(sp, screen)) {
            g.row = 1;
            Moved(c);
            SetHelp(c, g.row);
            W32(kPressed, pressed & ~kDown);
            W32(kHeld, held & ~kDown);
        }
        return;
    }
    // On the port's rows: the game sees no Up, Down, Cross or START.
    W32(kPressed, pressed & ~(kUp | kDown | kCross | kStart));
    W32(kHeld, held & ~(kUp | kDown));
    const int before = g.row;
    if (pressed & kUp) g.row -= 1;
    else if ((pressed & kDown) && g.row < 2) g.row += 1;
    if (g.row != before) {
        Moved(c);
        SetHelp(c, g.row);
    } else if (pressed & (kCross | kStart)) {
        Sound(c, 4);
        std::fprintf(stderr, "[menu] %s\n", g.row == 1 ? "PC SETTINGS: the PC settings panel" : "EXIT GAME");
        if (g.row == 1) ps2host::OpenPcMenu();
        else ps2host::QuitGame();
    }
}

namespace {

// 0x00191780 for the menu's highlight box while the cursor is on the port's
// rows: the game's own (on FILE) is not drawn.
void DrawBox(lifted::Context& c) {
    if (g.row && !g.own_highlight && g.sp && u32(c.r[4]) == g.sp + 0xED0) return;
    F_00191780(c);
}

// 0x0013CC40, a string drawn: on the main menu, the PLAYTIME and Item
// Recovery Rate line four lines lower, under the port's rows as the rows are
// spaced (it sat at the sixth row's bottom). PS2HOST_MENU_LOG=1 lists the
// menu's strings as drawn.
void DrawString(lifted::Context& c) {
    const u32 from = u32(c.r[31]);
    if (g.main && g.sp && u32(c.r[29]) == g.sp && (from == 0x0017A00Cu || from == 0x0017A038u))
        c.f[13] = FloatBits(BitsFloat(u32(c.f[13])) + 4.0f);
    static const bool log = std::getenv("PS2HOST_MENU_LOG") != nullptr;
    static int logged = 0;
    if (log && logged < 400 && g.sp && u32(c.r[29]) == g.sp) {
        ++logged;
        std::fprintf(stderr, "[menu] string from 0x%08X y %.1f \"%s\"\n", from, double(BitsFloat(u32(c.f[13]))),
                     ReadString(u32(c.r[4]), 40).c_str());
    }
    F_0013CC40(c);
}

}  // namespace

void RegisterMainMenu() {
    ps2host::Override(0x0013CC40u, DrawString, "a string (the main menu's PLAYTIME line under the PC rows)");
    ps2host::Override(0x00191780u, DrawBox, "a box object (the main menu's PC rows)");
}

bool IsMainMenuYield(const lifted::Context& c) { return u32(c.r[31]) == kMenuYieldReturn; }
