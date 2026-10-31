/*
 * Mister Mosquito at high frame rates: groundwork.
 *
 * The game steps 60 times a second, a step a picture: its main loop
 * (0x00168D60) runs the task scheduler (0x0011AB9C), where the tasks update
 * the game and write the next picture's display list, then waits for the
 * vertical blank (0x00100B40). The list goes to VIF1 as one chain at the
 * next frame's start, from 0x001352D0 (with a small GIF packet of the
 * display's from 0x00101B10). The drawing is written while the game
 * updates - no draw to run on its own between steps - so a picture between
 * two steps would be the two steps' lists blended (ps2host/display_list.h),
 * as in Resident Evil Outbreak's port.
 *
 * For now a probe: MM_BLEND_PROBE=1 keeps each step's list and plans the
 * blend from the step before, logging how the two lists match (their VIF
 * codes, segments matched, words blended, jumps, cuts) - whether blending
 * can work for this game, before any picture is made from it.
 */
#include <cstdio>
#include <cstdlib>

#include "ps2host/display_list.h"
#include "ps2host/host.h"
#include "ps2host/run.h"

void F_001352D0(lifted::Context& c);   // the frame's start: its display list sent (VIF1)

namespace {

bool g_in_send = false;                 // inside 0x001352D0
ps2host::Vif1Chain g_before, g_now;
bool g_have_before = false;
ps2host::ListBlendOptions g_options;

void Lists(ps2host::Vif1Chain& chain) {
    if (!g_in_send) return;
    g_now.words.assign(chain.words.begin(), chain.words.end());
    g_now.runs.assign(chain.runs.begin(), chain.runs.end());
    if (g_have_before) {
        ps2host::ListBlendPlan plan;
        const ps2host::ListBlend s = ps2host::PlanListBlend(g_before, g_now, g_options, plan);
        std::fprintf(stderr,
                     "[blend probe] frame %llu: %zu words, %llu codes%s; %u segments, %u moved, %u unmatched, "
                     "%u jumps; %llu words blended%s (worst %.2f, view %.2f)\n",
                     (unsigned long long)ps2host::FrameNumber(), g_now.words.size(), (unsigned long long)s.codes,
                     s.whole ? " (the same to their end)" : "", s.segments, s.moved, s.unmatched, s.jumps,
                     (unsigned long long)s.blended, s.cut ? ", a CUT" : "", s.worst, s.view);
    }
    // MM_BLEND_DUMP=<frame>: that frame's list and the next one's, as sent
    // (little-endian words), to blend_<frame>.bin in the working directory.
    static const long long dump = std::getenv("MM_BLEND_DUMP") ? std::atoll(std::getenv("MM_BLEND_DUMP")) : -1;
    const long long frame = (long long)ps2host::FrameNumber();
    if (dump >= 0 && (frame == dump || frame == dump + 1)) {
        char name[64];
        std::snprintf(name, sizeof name, "blend_%lld.bin", frame);
        if (FILE* f = std::fopen(name, "wb")) {
            std::fwrite(g_now.words.data(), 4, g_now.words.size(), f);
            std::fclose(f);
        }
    }
    std::swap(g_before, g_now);
    g_have_before = true;
}

void Send(lifted::Context& c) {
    g_in_send = true;
    F_001352D0(c);
    g_in_send = false;
}

}  // namespace

void RegisterHighFps() {
    if (!std::getenv("MM_BLEND_PROBE")) return;
    // (the probe's options: MM_BLEND_CUT_SHARE, MM_BLEND_JUMP)
    if (const char* v = std::getenv("MM_BLEND_CUT_SHARE")) g_options.cut_share = float(std::atof(v));
    if (const char* v = std::getenv("MM_BLEND_JUMP")) g_options.jump = float(std::atof(v));
    ps2host::SetVif1ChainFilter(Lists);
    ps2host::Override(0x001352D0u, Send, "the frame's display list sent: kept for the blend probe");
}
