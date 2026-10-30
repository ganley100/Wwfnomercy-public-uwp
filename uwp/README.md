# Xbox One Dev Mode (UWP / MSIX)

This folder builds WWF No Mercy: Recompiled as a UWP app for Xbox One (and Series) consoles in **Dev Mode**,
packaged as a signed `.msix` you install through the Xbox Device Portal. It uses the same toolchain as the desktop
build (clang-cl + Ninja) and the same code: `-DNMR_UWP=ON` switches the platform-specific paths.

> **Status: untested on hardware.** The UWP changes are written to compile and package. Whether the game boots on an
> Xbox One has not been verified yet; see [Risks](#risks).

## What's different from the desktop build

| Area | Desktop | UWP (`NMR_UWP`) |
|---|---|---|
| Entry point / window | `main`, Win32 window via SDL | `wWinMain` → `SDL_WinRTRunApp`, SDL's CoreWindow |
| Swap chain | `CreateSwapChainForHwnd` | `CreateSwapChainForCoreWindow` (plume patch) |
| Graphics API | D3D12, Vulkan fallback | D3D12 only (no Vulkan on Xbox) |
| Config, saves, stored ROM, mods, log | `%LOCALAPPDATA%\NoMercyRecompiled` | the app's `LocalState\NoMercyRecompiled` |
| Load ROM / install mods | Win32 file dialog | WinRT file picker; the file is copied into `LocalState\imports` |
| Fullscreen / window moves / monitor queries | user32 | n/a (always full screen) |
| HDR display query | DisplayConfig | off: SDR |
| CRT filter (librashader) | on | off (the DLL is desktop-only) |
| Ray tracing | DXR 1.1 | off (no DXR on Xbox One) |
| CRT | `vcruntime140.dll` app-local | `vcruntime140_app.dll` via the `Microsoft.VCLibs.140.00` framework package |

The UWP changes to the `lib/` submodules live in `uwp/patches/` because the submodules point at the upstream
jessetbh forks. `python uwp/apply_patches.py` applies them and is safe to re-run.

## Building

### GitHub Actions (recommended)

`.github/workflows/uwp.yml` runs on every push to `main` (and by hand from the Actions tab). It needs the same two
secrets as the desktop build:

- `NOMERCY_ROM_REPO`: `<owner>/<repo>` of a **private** repo with `nomercy.z64` (USA Rev 1) at its root
- `NOMERCY_CI_PAT`: a fine-grained token with read access to that repo

Optional: `UWP_PFX_BASE64` + `UWP_PFX_PASSWORD` (a code-signing `.pfx` with subject `CN=NoMercyRecompiled`) so every
build is signed with the same certificate. Without them each run makes a new self-signed certificate, and you have to
uninstall the old version before installing a new one.

**Without those secrets** the workflow still builds the app, just without the game code (`-DNMR_NO_GAME=ON`, artifact
`NoMercyRecompiled-Xbox-UWP-NoGame`). That build installs, starts and shows the launcher and settings, so it's the
way to check that the app runs on the console (renderer, controller, storage). Starting the game shows a message
instead, because the game code is generated from the ROM.

The `NoMercyRecompiled-Xbox-UWP` artifact holds the `.msix`, its `.cer`, and `Dependencies/Microsoft.VCLibs.x64.14.00.appx`.

### Locally (Windows)

Prerequisites: everything in [BUILDING.md](../BUILDING.md), plus the **Universal Windows Platform development**
workload and **C++ (v143) Universal Windows Platform tools** in Visual Studio / Build Tools 2022.

From a VS developer PowerShell (x64), after BUILDING.md steps 1–4 (clone, ROM, recompile + fixup pipeline):

```powershell
python uwp\apply_patches.py
.\uwp\build_sdl2.ps1 -OutDir build-sdl2-uwp
cmake -S . -B build-uwp -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DNMR_UWP=ON -DNMR_UWP_SDL2_DIR="$PWD\build-sdl2-uwp"
cmake --build build-uwp --target NoMercyRecompiled
.\uwp\package.ps1 -BuildDir build-uwp -OutDir uwp-package
```

Keep `uwp-package\NoMercyRecompiled.pfx` and pass it back with `-Pfx` for later builds. The default password is
`nomercy` (`-PfxPassword` to change it). It's a self-signed dev certificate: don't use it for anything else.

## Installing on the Xbox

1. Put the console in Dev Mode (Xbox Dev Mode app, Microsoft Partner Center developer account) and turn on
   **Device Portal** in Dev Home.
2. Open the Device Portal (`https://<xbox-ip>:11443`) → **Home → Add**, upload the `.msix`, then on the next page
   add `Microsoft.VCLibs.x64.14.00.appx` as a dependency. Install.
3. In Dev Home, highlight the app → **View details** → set **App type** to **Game**. As an "App" it only gets about
   1 GB of memory and part of the GPU. This step is required.
4. ROM: either launch, pick **Load ROM** and choose `nomercy.z64` from a USB drive, or copy it with the Device Portal's
   **File explorer** to `LocalAppData\NoMercyRecompiled_…\LocalState\nomercy.z64`. It's validated and stored on the
   next launch.

Logs (`NoMercyRecompiled.log` = stderr, `NoMercyRecompiled.out.log` = stdout), saves and config are under
`LocalState\NoMercyRecompiled\` in the same file explorer. Mods go in `LocalState\NoMercyRecompiled\mods\`.

## Icons

`python uwp/make_icons.py [source.png]` regenerates every icon from one square image (default `icons/boxart.png`):
`icons/app.png` (launcher thumbnail), `icons/app.ico` (desktop exe) and the MSIX tiles/splash in `uwp/Assets/`.

## Risks

These need real hardware to answer:

- **Shader model.** RT64's precompiled shaders target SM 6.3 (`lib_6_3`/`vs_6_3`/`ps_6_3`) and plume asks D3D12
  for SM 6.0+. Whether an Xbox One's UWP D3D12 device reports that is the make-or-break question. If device creation
  fails, the log says so (`[wcw] create_render_context`).
- **CPU.** The Xbox One's 1.75 GHz Jaguar cores support SSE4.1, so the build flags are fine, but they're much
  slower than a desktop CPU. Expect to need 30 fps (Settings → Graphics → Framerate) and native-ish resolution.
- **Memory.** It should fit in a Game's Dev Mode budget. It won't fit as an App (see step 3 above).
- **Desktop-only DLLs** (`user32`, `shell32`, `imm32`, `ole32`, `oleaut32`, `dbghelp`, `winmm`, `winhttp`,
  `dxcompiler`, `d3dcompiler_47`) are delay-loaded. If one is missing on the console, a call into it returns a stub
  value instead of crashing, and the log says `[uwp] stubbed <dll>!<function>`. `uwp/package.ps1` refuses an exe that
  imports anything else outright.

## If it crashes

Open the Device Portal's **File explorer** → `LocalAppData\NoMercyRecompiled_…\LocalState\NoMercyRecompiled\`:

- **No `NoMercyRecompiled.log` at all**: the exe never ran its first line. Check that `Microsoft.VCLibs.x64.14.00.appx`
  was installed with it, and that **App type** is **Game**.
- **The log ends in `==== [wcw] unhandled exception 0x… ====`**: the next line gives the faulting address as
  `NoMercyRecompiled.exe +0x…`. Look that offset up in `NoMercyRecompiled.map` from the `-DebugSyms` artifact of the
  same run.
- **Otherwise** the last lines show how far it got (`[wcw] priming DXGI + D3D12`, `[wcw] create_render_context`, …).
