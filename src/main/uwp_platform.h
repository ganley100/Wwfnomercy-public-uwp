// UWP (Xbox One Dev Mode) platform layer, see uwp_platform.cpp.
#pragma once
#if defined(NMR_UWP)

#include <cstdint>

// The app's LocalState folder: config, saves, the stored ROM, mods and the log all live under it.
extern "C" const wchar_t* nmr_uwp_local_folder();

// The package's install folder (read-only): assets/, recompcontrollerdb.txt and the bundled mods.
extern "C" const wchar_t* nmr_uwp_install_folder();

// The CoreWindow's size in pixels (used by plume's swap chain, which can't read the CoreWindow off its thread).
extern "C" void nmr_uwp_get_window_size(void* core_window, uint32_t* width, uint32_t* height);

// The ROM drop folders: D:\DevelopmentFiles\NoMercy (reachable from the Device Portal's file explorer), then
// \NoMercy on any USB drive (E: to Z:). Creates each folder and a "Put game here.txt" note in it if they're missing.
// When copy_rom is set, the first .z64/.n64/.v64 found (D: first) is copied to dest_path (in LocalState, where the
// app can read it) and true is returned.
extern "C" bool nmr_uwp_rom_drop_folders(bool copy_rom, const wchar_t* dest_path);

#endif
