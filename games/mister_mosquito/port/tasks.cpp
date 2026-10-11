/*
 * Mister Mosquito's task system, on host fibers.
 *
 * The game runs most of its logic as cooperative tasks, each with a stack of
 * its own (FINDINGS.md, "The task system"). A task control block (TCB) keeps
 * what a task needs to resume:
 *
 *   +0x000  u16 flags     0x8000 sleeping (with a frame count), 0x4000 held
 *   +0x010  next TCB      the run list, a ring through the list's head
 *   +0x024  s32 frames    left to sleep
 *   +0x02C  saved $sp     +0x030 resume address
 *   +0x0F0  $fp, $gp, $s0-$s7 (128 bits each), +0x190 $f0-$f31
 *
 * and the system's globals at 0x0039B090: +0x00 the list's head, +0x08 the
 * task running, +0x14 where a task's switch returns to in the scheduler,
 * +0x18 the scheduler's own $sp, +0x1C the address it resumed.
 *
 * Once a frame the main loop calls the scheduler (0x0011AB9C), which loads
 * each runnable task's registers and jumps into it; a task gives the processor
 * back by saving its registers and jumping into the scheduler's loop - yield
 * (0x0011AF7C), sleep for a number of frames (0x0011AE98), or killing itself
 * (0x0011AD74). Those jumps leave one guest stack for another, which nested
 * native calls cannot do: the translated yield would simply return to its
 * task. So each task runs on a host coroutine (engine run.h): the scheduler
 * here switches to a task's coroutine where the game jumped into the task,
 * and the switch routines switch back where the game jumped out. Memory -
 * the TCBs and globals - is kept exactly as the game's own code keeps it.
 */

#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/memory.h"
#include "ps2host/run.h"

void F_0011AB9C(lifted::Context& c);   // the scheduler
void F_0011AD74(lifted::Context& c);   // kill a task

namespace {

constexpr u32 kGlobals = 0x0039B090u;
constexpr u32 kSchedulerLoop = 0x0011ABECu;     // where the switch routines jump back to
constexpr int kSaved[] = {30, 28, 16, 17, 18, 19, 20, 21, 22, 23};   // fp gp s0-s7, from TCB+0xF0

struct Task {
    ps2host::Coroutine* co = nullptr;
    u32 pc = 0, sp = 0;             // where its coroutine stands suspended
};

std::map<u32, Task> g_tasks;                    // by TCB
std::vector<ps2host::Coroutine*> g_dropped;     // coroutines no task resumes: freed from the scheduler

u32 R32(u32 a) { return u32(ps2host::GuestMemory().Read(a, 4)); }
u16 R16(u32 a) { return u16(ps2host::GuestMemory().Read(a, 2)); }
void W32(u32 a, u32 v) { ps2host::GuestMemory().Write(a, v, 4); }
void W16(u32 a, u16 v) { ps2host::GuestMemory().Write(a, v, 2); }
u64 Sx(u32 v) { return u64(s64(s32(v))); }

void Drop(u32 tcb) {
    auto found = g_tasks.find(tcb);
    if (found == g_tasks.end()) return;
    if (found->second.co) g_dropped.push_back(found->second.co);
    g_tasks.erase(found);
}

// What the switch routines store: $sp, the resume address, the registers.
void SaveTask(lifted::Context& c, u32 tcb) {
    ps2host::Memory& m = ps2host::GuestMemory();
    W32(tcb + 0x2C, u32(c.r[29]));
    W32(tcb + 0x30, u32(c.r[31]));
    for (int i = 0; i < 10; ++i) {
        m.Write(tcb + 0xF0 + 16 * i, c.r[kSaved[i]], 8);
        m.Write(tcb + 0xF8 + 16 * i, c.rh[kSaved[i]], 8);
    }
    for (int i = 0; i < 32; ++i) W32(tcb + 0x190 + 4 * i, c.f[i]);
}

// What the scheduler loads before it jumps into a task.
void LoadTask(lifted::Context& c, u32 tcb) {
    ps2host::Memory& m = ps2host::GuestMemory();
    c.r[29] = Sx(R32(tcb + 0x2C));
    for (int i = 0; i < 10; ++i) {
        c.r[kSaved[i]] = m.Read(tcb + 0xF0 + 16 * i, 8);
        c.rh[kSaved[i]] = m.Read(tcb + 0xF8 + 16 * i, 8);
    }
    for (int i = 0; i < 32; ++i) c.f[i] = R32(tcb + 0x190 + 4 * i);
}

// Back to the scheduler from the running task's coroutine: the game's switch
// jumped to the scheduler's loop; the coroutine resumes here when the
// scheduler next jumps to where this task said it would go on.
void SwitchOut(lifted::Context& c, u32 tcb) {
    Task& task = g_tasks[tcb];
    task.pc = u32(c.r[31]);
    task.sp = u32(c.r[29]);
    c.r[31] = Sx(R32(kGlobals + 0x14));
    ps2host::SwitchCoroutine(nullptr);
}

// The scheduler's jump into a task: its suspended coroutine when it stands
// where the TCB says to go on, a new one starting there otherwise.
void Resume(lifted::Context& c, u32 tcb) {
    const u32 pc = R32(tcb + 0x30), sp = R32(tcb + 0x2C);
    W32(kGlobals + 0x1C, pc);
    LoadTask(c, tcb);
    c.r[2] = Sx(pc);
    c.r[3] = Sx(kGlobals);
    Task& task = g_tasks[tcb];
    if (!task.co || ps2host::CoroutineFinished(task.co) || task.pc != pc || task.sp != sp) {
        if (std::getenv("PS2HOST_TASK_LOG"))
            std::fprintf(stderr, "[task] TCB 0x%08X starts at 0x%08X (sp 0x%08X)%s\n", tcb, pc, sp,
                         task.co ? " - a new start for a task that was suspended" : "");
        if (task.co) g_dropped.push_back(task.co);
        task.co = ps2host::CreateCoroutine([&c, pc, tcb] {
            ps2host::Dispatch(c, pc);
            // A task's code never returns on the console: it would leave the
            // scheduler from the task's stack. Say so; the task is dropped.
            ps2host::Note("task returned " + std::to_string(pc),
                          "tasks: the task at TCB " + std::to_string(tcb) + " returned from its code");
        });
        task.pc = task.sp = 0;
    }
    ps2host::SwitchCoroutine(task.co);
    if (ps2host::CoroutineFinished(task.co)) Drop(tcb);
}

// 0x0011AB9C: once a frame, every runnable task until it switches out.
void Scheduler(lifted::Context& c) {
    if (ps2host::CurrentCoroutine()) throw ps2host::Unimplemented{"tasks: the scheduler run from inside a task"};
    lifted::Context saved = c;
    W32(kGlobals + 0x08, R32(kGlobals + 0x00));
    W32(kGlobals + 0x18, u32(c.r[29]));
    W32(kGlobals + 0x14, kSchedulerLoop);
    for (;;) {
        // The game's loop head (0x0011ABEC): interrupts are delivered and the
        // run's limits checked here, as at any translated loop's.
        lifted::Tick(c, kSchedulerLoop);
        for (ps2host::Coroutine* co : g_dropped) ps2host::DeleteCoroutine(co);
        g_dropped.clear();
        const u32 head = R32(kGlobals + 0x00);
        const u32 tcb = R32(R32(kGlobals + 0x08) + 0x10);
        if (tcb == head) break;
        W32(kGlobals + 0x08, tcb);
        const s16 flags = s16(R16(tcb));
        if (flags & 0xC000) {
            if (flags >= 0) continue;                       // held
            const u32 frames = R32(tcb + 0x24) - 1;         // sleeping
            W16(tcb + 0x24, u16(frames));
            if (s32(frames) > 0) continue;
            W16(tcb, u16(flags) & 0x7FFF);
        }
        Resume(c, tcb);
    }
    W32(kGlobals + 0x1C, 0);
    W32(kGlobals + 0x08, 0);
    for (int r : {16, 17, 18, 19, 20, 21, 22, 23, 28, 29, 30, 31}) {
        c.r[r] = saved.r[r];
        c.rh[r] = saved.rh[r];
    }
}

u32 Running(const char* what) {
    const u32 tcb = R32(kGlobals + 0x08);
    auto found = g_tasks.find(tcb);
    if (!ps2host::CurrentCoroutine() || found == g_tasks.end() ||
        found->second.co != ps2host::CurrentCoroutine())
        throw ps2host::Unimplemented{std::string("tasks: ") + what + " outside a task's coroutine"};
    return tcb;
}

// PS2HOST_TITLE_LOG=1: the title task's state at its yields (F_0019C790's
// frame: +0x110, +0x180 its START window's counter, +0x200), every 30th.
void TitleLog(const lifted::Context& c) {
    static const bool on = std::getenv("PS2HOST_TITLE_LOG") != nullptr;
    static u32 count = 0;
    const u32 ra = u32(c.r[31]), sp = u32(c.r[29]);
    if (!on || ra < 0x0019C790u || ra >= 0x001A2F18u || count++ % 30) return;
    std::fprintf(stderr, "[title] tcb 0x%08X yield at 0x%08X: 110=%d 180=%d 200=%d 1e0=%d pad pressed %04X held %04X\n",
                 R32(kGlobals + 0x08), ra,
                 s32(R32(sp + 0x110)), s32(R32(sp + 0x180)), s32(R32(sp + 0x200)), s32(R32(sp + 0x1E0)),
                 R32(0x0031C1EC), R32(0x0031C1E8));
}

// 0x0011AF7C: give the processor back until the next frame.
void Yield(lifted::Context& c) {
    TitleLog(c);
    const u32 tcb = Running("yield");
    SaveTask(c, tcb);
    SwitchOut(c, tcb);
}

// 0x0011AE98: sleep $a0 frames.
void Sleep(lifted::Context& c) {
    const u32 tcb = Running("sleep");
    W32(tcb + 0x24, u32(c.r[4]));
    W16(tcb, u16(R16(tcb) | 0x8000));
    SaveTask(c, tcb);
    SwitchOut(c, tcb);
}

// 0x0011AD74: take the task at $a0 off the run list. A task that kills itself
// goes back to the scheduler and is never resumed.
void Kill(lifted::Context& c) {
    const u32 tcb = u32(c.r[4]);
    const bool self = ps2host::CurrentCoroutine() && tcb == R32(kGlobals + 0x08);
    if (std::getenv("PS2HOST_TASK_LOG"))
        std::fprintf(stderr, "[task] TCB 0x%08X killed%s, from 0x%08X (running: TCB 0x%08X)\n", tcb,
                     self ? " (itself)" : "", u32(c.r[31]), R32(kGlobals + 0x08));
    F_0011AD74(c);
    if (!self) {
        Drop(tcb);
        return;
    }
    Drop(tcb);
    ps2host::SwitchCoroutine(nullptr);                     // never resumed
}

}  // namespace

void RegisterTaskOverrides() {
    ps2host::Override(0x0011AB9Cu, Scheduler, "task scheduler (tasks on host coroutines)");
    ps2host::Override(0x0011AF7Cu, Yield, "task yield");
    ps2host::Override(0x0011AE98u, Sleep, "task sleep");
    ps2host::Override(0x0011AD74u, Kill, "task kill");
}
