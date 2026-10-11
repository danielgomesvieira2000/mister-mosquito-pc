/*
 * PS2HOST_SOUND_LOG=1: Mister Mosquito's sound command handshake, traced
 * (FINDINGS.md, "Sound"). The EE queues commands for the IOP's driver
 * (0x001BF0D0), its sound thread sends the queue once a frame (0x001BEC18 and
 * on) and a waiter (0x001C2690) spins until the driver's processed count, at
 * the status the driver answers (+0x1C0), reaches the request number
 * (0x00666E48). Each is logged with the sound thread's frame.
 */

#include <cstdio>
#include <cstdlib>

#include "ps2host/host.h"
#include "ps2host/memory.h"
#include "ps2host/run.h"

void F_001C2690(lifted::Context& c);
void F_001BF0D0(lifted::Context& c);
void F_001BEC18(lifted::Context& c);

namespace {

constexpr u32 kState = 0x00666E00;     // +0x3C buffer, +0x40 queued, +0x48 request number
u32 g_frame = 0;

u32 R32(u32 a) { return u32(ps2host::GuestMemory().Read(a, 4)); }
u32 Status() { return R32(R32(0x00669080) + 0x1C0); }

void Wait(lifted::Context& c) {
    const u32 mode = u32(c.r[4]);
    std::fprintf(stderr, "[snd] f%u wait(%u) request %u done %u queued %u\n", g_frame, mode,
                 R32(kState + 0x48), Status(), R32(kState + 0x40));
    F_001C2690(c);
    std::fprintf(stderr, "[snd] f%u wait(%u) -> %d (request %u done %u)\n", g_frame, mode, int(c.r[2]),
                 R32(kState + 0x48), Status());
}

void Enqueue(lifted::Context& c) {
    const u32 op = u32(c.r[4]), a1 = u32(c.r[5]);
    F_001BF0D0(c);
    std::fprintf(stderr, "[snd] f%u queue op 0x%X %08X -> %u queued\n", g_frame, op, a1, R32(kState + 0x40));
}

void Frame(lifted::Context& c) {
    ++g_frame;
    const u32 before = R32(kState + 0x40);
    F_001BEC18(c);
    std::fprintf(stderr, "[snd] f%u sent %u (done %u, request %u)\n", g_frame, before, Status(),
                 R32(kState + 0x48));
}

}  // namespace

void RegisterSoundTrace() {
    if (!std::getenv("PS2HOST_SOUND_LOG")) return;
    ps2host::Override(0x001C2690u, Wait, "sound wait (traced)");
    ps2host::Override(0x001BF0D0u, Enqueue, "sound queue (traced)");
    ps2host::Override(0x001BEC18u, Frame, "sound frame (traced)");
}
