// Stand-ins for the ROM-generated code, for a build made without the ROM (cmake -DNMR_NO_GAME=ON).
// N64Recomp and RSPRecomp normally generate the recompiled game (RecompiledFuncs/) and its audio microcode
// (rsp/nomercy_audio.cpp) from nomercy.z64. Without them the app still builds and runs up to the launcher (menus,
// settings, controller and renderer setup), which is enough to check that it starts on a device; starting the game
// shows a message instead.
#include <cstdint>

#include "recomp.h"
#include "librecomp/rsp.hpp"
#include "ultramodern/error_handling.hpp"

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram; (void)ctx;
    ultramodern::error_handling::message_box(
        "This build of WWF No Mercy: Recompiled has no game code: it was made without the ROM (NMR_NO_GAME).\n\n"
        "It can only show the launcher and settings. Build with the ROM to play.");
    ULTRAMODERN_QUICK_EXIT();
}

// Called by src/game/recomp_api.cpp's osDriveRomInit wrapper, which only the game code reaches.
extern "C" void osDriveRomInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram; (void)ctx;
}

RspExitReason nomercy_audio_ucode(uint8_t* rdram, uint32_t ucode_addr) {
    (void)rdram; (void)ucode_addr;
    return RspExitReason::Broke;
}
