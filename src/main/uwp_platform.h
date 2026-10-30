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

#endif
