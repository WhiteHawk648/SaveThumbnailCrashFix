/**
 * Save Thumbnail Crash Fix - NVSE
 * 
 * Fixes a notorious Gamebryo engine null-pointer crash in Fallout: New Vegas 1.4.0.525.
 * Discovered via reverse engineering and fnv_decomp audit:
 *   - src/fallout/misc/screenshot.cpp:413 (CreateOffscreenPlainSurface returns NULL on D3D failure)
 *   - src/fallout/misc/saveload/bgssaveloadheader.cpp:187 (Unchecked pixels->NiPixelData_GetPixels)
 *   - src/staging/blk_00640000_od.cpp:1515 (0x0064DAC0 NiPixelData_GetPixels dereferences self + 0x50)
 * 
 * Author: Gavin & Antigravity
 * License: MIT
 */

#include "../include/nvse_minimal.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

// 4MB zero-filled fallback buffer providing safe black thumbnail pixel data
static char g_dummyBlackPixels[1024 * 1024 * 4] = {0};

static HMODULE g_hModule = NULL;
static char g_iniPath[MAX_PATH] = {0};
static char g_logPath[MAX_PATH] = {0};
static FILE* g_logFile = NULL;

// Configuration settings
static int   g_bEnabled = 1;
static int   g_bEnableLogging = 1;
static int   g_bShowInGameNotification = 0;
static float g_fNotificationDuration = 3.0f;
static unsigned int g_crashesPrevented = 0;

// Gamebryo engine HUD notification function (QueueUIMessage / DispatchMessageToSink at 0x007052f0)
typedef bool (__cdecl *QueueUIMessage_t)(const char* text, int emotion, const char* iconPath, const char* soundName, float duration, bool bInstant);

void LogMessage(const char* fmt, ...)
{
    if (!g_bEnableLogging) return;

    if (!g_logFile && g_logPath[0] != '\0')
    {
        g_logFile = fopen(g_logPath, "a");
    }
    if (g_logFile)
    {
        char timeBuf[32];
        time_t now = time(NULL);
        struct tm* tmInfo = localtime(&now);
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tmInfo);

        fprintf(g_logFile, "[%s] ", timeBuf);

        va_list args;
        va_start(args, fmt);
        vfprintf(g_logFile, fmt, args);
        va_end(args);
        fprintf(g_logFile, "\n");
        fflush(g_logFile);
    }
}

static void LoadConfiguration()
{
    if (g_iniPath[0] == '\0') return;

    g_bEnabled = GetPrivateProfileIntA("General", "bEnabled", 1, g_iniPath);
    g_bEnableLogging = GetPrivateProfileIntA("General", "bEnableLogging", 1, g_iniPath);
    g_bShowInGameNotification = GetPrivateProfileIntA("General", "bShowInGameNotification", 0, g_iniPath);

    char durationStr[32] = {0};
    GetPrivateProfileStringA("General", "fNotificationDuration", "3.0", durationStr, sizeof(durationStr), g_iniPath);
    g_fNotificationDuration = (float)atof(durationStr);
    if (g_fNotificationDuration <= 0.0f) g_fNotificationDuration = 3.0f;

    LogMessage("[Config] bEnabled = %d", g_bEnabled);
    LogMessage("[Config] bEnableLogging = %d", g_bEnableLogging);
    LogMessage("[Config] bShowInGameNotification = %d", g_bShowInGameNotification);
    LogMessage("[Config] fNotificationDuration = %.1f", g_fNotificationDuration);
}

// -----------------------------------------------------------------------------
// Hook: NiPixelData_GetPixels (0x0064DAC0)
// Gamebryo __thiscall calling convention (this in ECX, callee cleans stack)
// -----------------------------------------------------------------------------

extern "C" __attribute__((fastcall)) void* Hooked_NiPixelData_GetPixels(void* pThis, void* /*dummy_edx*/, int a, int b)
{
    if (!pThis)
    {
        g_crashesPrevented++;
        LogMessage("[CRASH PREVENTED #%u] Intercepted NULL NiPixelData pointer in BGSSaveLoadHeader_SaveHeader! Fallback black buffer supplied (mip=%d, face=%d).",
                   g_crashesPrevented, a, b);

        if (g_bShowInGameNotification)
        {
            QueueUIMessage_t QueueUIMessage = (QueueUIMessage_t)0x007052f0;
            QueueUIMessage("Save thumbnail failed — crash prevented", 0,
                           "Interface\\Icons\\Message Icons\\glow_message_vaultboy_sad.dds",
                           NULL, g_fNotificationDuration, false);
        }

        return g_dummyBlackPixels;
    }

    char* self = (char*)pThis;
    int p50 = *(int*)(self + 0x50);
    int* p5C = *(int**)(self + 0x5C);
    int p60 = *(int*)(self + 0x60);

    if (!p5C)
    {
        g_crashesPrevented++;
        LogMessage("[CRASH PREVENTED #%u] Intercepted NULL pixel offset table (0x5C) in NiPixelData! Fallback black buffer supplied.",
                   g_crashesPrevented);

        if (g_bShowInGameNotification)
        {
            QueueUIMessage_t QueueUIMessage = (QueueUIMessage_t)0x007052f0;
            QueueUIMessage("Save thumbnail failed — crash prevented", 0,
                           "Interface\\Icons\\Message Icons\\glow_message_vaultboy_sad.dds",
                           NULL, g_fNotificationDuration, false);
        }

        return g_dummyBlackPixels;
    }

    // Original Gamebryo formula:
    // return *(int*)(self + 0x50) + b * *(int*)(*(int*)(self + 0x5C) + *(int*)(self + 0x60) * 4) + *(int*)(*(int*)(self + 0x5C) + a * 4);
    return (void*)(p50 + b * p5C[p60] + p5C[a]);
}

static void WriteRelJump(uintptr_t jumpSrc, uintptr_t jumpTgt)
{
    DWORD oldProtect;
    VirtualProtect((void*)jumpSrc, 5, PAGE_EXECUTE_READWRITE, &oldProtect);

    uint8_t* pSrc = (uint8_t*)jumpSrc;
    pSrc[0] = 0xE9; // JMP rel32
    *(uint32_t*)(pSrc + 1) = (uint32_t)(jumpTgt - jumpSrc - 5);

    VirtualProtect((void*)jumpSrc, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), (void*)jumpSrc, 5);
}

void InstallHooks()
{
    if (!g_bEnabled)
    {
        LogMessage("[Init] Mod is disabled via INI (bEnabled = 0). Skipping hook installation.");
        return;
    }

    LogMessage("[Init] Installing NiPixelData_GetPixels safety patch at 0x0064DAC0...");
    WriteRelJump(0x0064DAC0, (uintptr_t)&Hooked_NiPixelData_GetPixels);
    LogMessage("[Init] Patch successfully installed. Gamebryo save thumbnails are now crash-proof.");
}

// -----------------------------------------------------------------------------
// DllMain: Resolves DLL-relative paths for INI and Log
// -----------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        g_hModule = (HMODULE)hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);

        char modulePath[MAX_PATH];
        if (GetModuleFileNameA(g_hModule, modulePath, MAX_PATH))
        {
            // Replace .dll with .ini and .log
            char* dot = strrchr(modulePath, '.');
            if (dot)
            {
                *dot = '\0';
                snprintf(g_iniPath, sizeof(g_iniPath), "%s.ini", modulePath);
                snprintf(g_logPath, sizeof(g_logPath), "%s.log", modulePath);
            }
        }
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        if (g_logFile)
        {
            if (g_crashesPrevented > 0)
            {
                LogMessage("[Shutdown] Session ended. Total save crashes prevented: %u", g_crashesPrevented);
            }
            fclose(g_logFile);
            g_logFile = NULL;
        }
    }
    return TRUE;
}

// -----------------------------------------------------------------------------
// xNVSE Plugin Interface Exports
// -----------------------------------------------------------------------------

PluginInfo g_pluginInfo = {
    PluginInfo::kInfoVersion,
    "SaveThumbnailCrashFix",
    1 // Version 1.0.0
};

extern "C" {

__declspec(dllexport) bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info)
{
    info->infoVersion = g_pluginInfo.infoVersion;
    info->name        = g_pluginInfo.name;
    info->version     = g_pluginInfo.version;

    if (nvse->isEditor)
    {
        return false;
    }

    if (nvse->runtimeVersion != RUNTIME_VERSION_1_4_0_525 &&
        nvse->runtimeVersion != RUNTIME_VERSION_1_4_0_525_ALT)
    {
        LogMessage("[Error] Unsupported Fallout NV runtime version 0x%08X (expected 0x%08X)",
                   nvse->runtimeVersion, RUNTIME_VERSION_1_4_0_525);
        return false;
    }

    return true;
}

__declspec(dllexport) bool NVSEPlugin_Load(const NVSEInterface* nvse)
{
    LoadConfiguration();
    LogMessage("==================================================================");
    LogMessage("Save Thumbnail Crash Fix - NVSE v1.0.0 loaded successfully");
    LogMessage("Target: Fallout: New Vegas 1.4.0.525 (Gamebryo Engine)");
    LogMessage("==================================================================");

    InstallHooks();
    return true;
}

}
