#pragma once

#include <windows.h>
#include <stdint.h>

// Fallout: New Vegas 1.4.0.525 runtime version in xNVSE:
// Packed as ((minor << 24) | (build << 16) | (sub << 4)) = 0x040020D0
#define RUNTIME_VERSION_1_4_0_525 0x040020D0
#define RUNTIME_VERSION_1_4_0_525_ALT 0x0104020D

struct PluginInfo
{
    enum { kInfoVersion = 1 };
    uint32_t    infoVersion;
    const char* name;
    uint32_t    version;
};

struct NVSEInterface
{
    uint32_t    nvseVersion;
    uint32_t    runtimeVersion;
    uint32_t    editorVersion;
    uint32_t    isEditor;
};
