/*
 * Mister Mosquito's host implementations of guest functions.
 *
 * An override replaces the translated function at its address: the way a port
 * redirects code that drives hardware the host provides differently, and the
 * way the PC features hook in (a settings menu in the game's own menus,
 * widescreen, the mouse's targets). Register them here with
 * ps2host::Override(address, function, "why"); the original stays callable as
 * F_<ADDRESS>(c). See the engine's docs/PORTING.md and the ps2-port skill.
 *
 * Override as little as possible: the game's own code is the point.
 */

#include "ps2host/host.h"
#include "ps2host/iop_vm.h"
#include "ps2host/run.h"

void RegisterTaskOverrides();    // tasks.cpp
void RegisterSoundTrace();       // sound_trace.cpp

void RegisterGameOverrides() {
    RegisterTaskOverrides();
    RegisterSoundTrace();
    // The game's own sound driver (server "sndn", 0x736E646E) runs in the IOP
    // interpreter on the host's libsd, as the console runs it.
    ps2host::iop::RunModule("SNDN2DRV.IRX");
}
