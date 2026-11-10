# Save Thumbnail Crash Fix - NVSE

An ultra-lightweight, zero-dependency xNVSE plugin for **Fallout: New Vegas** that eliminates the Gamebryo engine null-pointer crash during savegame thumbnail generation.

---

## The Bug: Root Cause Analysis

In Fallout: New Vegas, every time the game saves (manual save, quicksave, autosave, or script save), Gamebryo takes a screenshot of the viewport to store as a 512x512 thumbnail in the savegame header (`.fos`).

By auditing the engine disassembly and full decompilation source (`fnv_decomp`), the root cause was pinpointed across three functions:

### 1. `ScreenShot_TakeSaveGameScreenShot` (`src/fallout/misc/screenshot.cpp:413`)
When Direct3D9 surface creation fails (due to VRAM pressure, alt-tab, display server desync, DXVK surface reallocation, or temporary device loss), the engine logs:
```
DEFAULT: (ScreenShot) Unable to create image surface.
```
and returns `0` (`NULL`) instead of a valid `NiPixelData` object.

### 2. `BGSSaveLoadHeader_SaveHeader` (`src/fallout/misc/saveload/bgssaveloadheader.cpp:186-190`)
Bethesda implemented an interface menu check, but **omitted a NULL check** on the returned `pixels` pointer:
```cpp
if (!DAT_011de134->InterfaceManager_GetMenuMode())
    file->WriteToFile(pixels->NiPixelData_GetPixels(0, 0), height5 * width2 * 3); // <-- CRASH if pixels == NULL
else
    file->WriteToFile(0, height5 * width2 * 3);
delete pixels;
```

### 3. `NiPixelData_GetPixels` (`0x0064DAC0` / `blk_00640000_od.cpp:1515`)
When called with `this == NULL`, the function immediately dereferences offset `0x50` (`this + 0x50`):
```nasm
0064DAC0  mov  eax, [ecx+50h]    ; <-- SEGFAULT (Read access violation at 0x00000050)
0064DAC3  mov  edx, [esp+08h]
```
The game process abruptly dies to desktop with an unhandled access violation `EXCEPTION_ACCESS_VIOLATION (0xC0000005)`.

---

## Why Existing Bugfix Mods Missed This

Even comprehensive guide setups like **Viva New Vegas** crash here because:
- **NVTF (New Vegas Tick Fix)** optimizes framerate scheduling and D3D9 texture pooling, but does not intercept the save header writing routine.
- **lStewieAl's Tweaks** provides hundreds of engine tweaks, but no guard on `NiPixelData_GetPixels`.
- **YUP (Yukichigai Unofficial Patch)** is an ESM patch for game records, unable to alter engine C++ binaries.

---

## How This Mod Fixes It

`Save Thumbnail Crash Fix - NVSE` installs a micro-hook directly at `0x0064DAC0` (`NiPixelData_GetPixels`):
1. **Null Pointer Guard**: If `pThis == NULL` (or if internal pixel offset table `0x5C` is NULL), the hook intercepts execution before the `0x50` dereference can occur.
2. **Safe Black Fallback**: It returns a pointer to a static 4 MB zeroed buffer (`g_dummyBlackPixels`). The engine writes black pixels into the save thumbnail slot and continues cleanly.
3. **Save Integrity Preserved**: The save file completes without corruption. When viewed in the Load menu, the save displays normally.
4. **Zero Overhead**: In normal saves where screenshot creation succeeds, the hook executes the exact 3-instruction Gamebryo calculation with negligible latency (< 1 nanosecond).

---

## Configuration (`SaveThumbnailCrashFix.ini`)

Located in `Data/NVSE/Plugins/SaveThumbnailCrashFix.ini`:

```ini
[General]
; Master toggle. 1 = enabled, 0 = disabled.
bEnabled = 1

; Write diagnostic events to SaveThumbnailCrashFix.log.
bEnableLogging = 1

; Show a brief corner HUD message (Vault-Boy) when a crash is caught.
; 0 = Silent (recommended), 1 = Show in-game HUD alert.
bShowInGameNotification = 0

; Notification duration in seconds.
fNotificationDuration = 3.0
```

---

## Building from Source

The plugin compiles into a standalone 32-bit Windows PE DLL with no external runtime dependencies (linking statically with minimal C runtime).

### Using Zig (Cross-platform on Linux/macOS/Windows)
```bash
make
```
Or directly:
```bash
zig c++ -target x86-windows-gnu -O2 -Wall -Wextra -shared -s -I./include src/main.cpp -o SaveThumbnailCrashFix.dll
```

---

## Requirements & Compatibility
- **Fallout: New Vegas** version 1.4.0.525
- **xNVSE** 6.3.0 or higher
- 100% compatible with Viva New Vegas, TTW, Stewie Tweaks, NVTF, NVAC, and all standard mods.
