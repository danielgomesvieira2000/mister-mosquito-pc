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
void F_001C2880(lifted::Context& c);

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

// PS2HOST_STOP_TRACKS_ON_UNLOAD=1, a diagnostic and not a fix: unloading a
// bank (0x001C2880) also stops the sequencer's tracks that play from it, so
// the stage-entry stall (FINDINGS.md) is stepped past to see what follows.
void UnloadBank(lifted::Context& c) {
    const u32 bank = u32(c.r[4]);
    std::fprintf(stderr, "[snd] unload bank %u\n", bank);
    ps2host::Memory& m = ps2host::GuestMemory();
    for (u32 t = 0; t < 48; ++t) {
        const u32 track = 0x00665E40u + 0x54u * t;
        if ((m.Read(track, 4) & 0xF) != 0 && m.Read(track + 0x18, 2) == bank) {
            std::fprintf(stderr, "[snd] bank %u unloaded: track %u stopped (flags 0x%X)\n", bank, t,
                         u32(m.Read(track, 4)));
            for (u32 i = 0; i < 0x54; i += 4) m.Write(track + i, 0, 4);
        }
    }
    F_001C2880(c);
}

}  // namespace

void RegisterSoundTrace() {
    if (std::getenv("PS2HOST_STOP_TRACKS_ON_UNLOAD"))
        ps2host::Override(0x001C2880u, UnloadBank, "bank unload stops its tracks (diagnostic)");
    if (!std::getenv("PS2HOST_SOUND_LOG")) return;
    ps2host::Override(0x001C2690u, Wait, "sound wait (traced)");
    ps2host::Override(0x001BF0D0u, Enqueue, "sound queue (traced)");
    ps2host::Override(0x001BEC18u, Frame, "sound frame (traced)");
}
