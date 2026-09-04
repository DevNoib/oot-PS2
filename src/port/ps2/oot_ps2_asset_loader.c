#include "oot_port_asset_loader.h"
#include "oot_ps2_asset_setup.h"
#include "oot_port_audio_backend.h"
#include "oot_port_memory.h"
#include "segment_symbols.h"
#include "oot_ps2_platform.h"
#include "oot_ps2_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* OotPort_GetAssetRoot(void) {
    return NULL;
}

s32 OotPort_SetAssetRoot(const char* root) {
    return 0;
}

s32 OotPort_IsDiscBoot(void) {
    return 0;
}

s32 OotPort_AssetInit(const char* executablePath) {
    return 0;
}

void OotPort_AssetNotifyResume(void) {
}

const char* OotPort_ResolveRootPath(const char* path, char* buffer, size_t bufferSize) {
    return NULL;
}

const void* OotPort_GetCachedAssetPointer(uintptr_t vrom, size_t size) {
    return NULL;
}

s32 OotPs2Asset_IsMutableTextureRange(uintptr_t start, uintptr_t end) {
    return 0;
}

uintptr_t OotPort_NormalizeVrom(uintptr_t vrom) {
    return 0;
}

void OotPort_NormalizeRomFile(RomFile* file) {
}

const OotPortMessageEntry* OotPort_FindMessageEntry(const OotPortMessageEntry* entries, size_t count, u16 textId) {
    return NULL;
}

s32 OotPort_GetLoadedExternalAssetRangeFlags(const void* ptr, size_t size, u32* flags) {
    return 0;
}

s32 OotPort_MarkLoadedExternalAssetRangeFlags(const void* ptr, size_t size, u32 flags) {
    return 0;
}

s32 OotPort_IsLoadedNativeExternalAssetRange(const void* ptr, size_t size) {
    return 0;
}

s32 OotPort_IsNativeExternalTextureRange(const void* ptr, size_t size) {
    return 0;
}

u32 OotPort_GetExternalAssetRangeSerial(const void* ptr, size_t size) {
    return 0;
}

s32 OotPort_GetNativeExternalTextureMappingRange(const void* ptr, uintptr_t* ramStart, uintptr_t* ramEnd) {
    return 0;
}

s32 OotPort_GetNativeExternalTextureRangeStart(const void* ptr, size_t size, uintptr_t* rangeStart) {
    return 0;
}

s32 OotPort_MapNativeExternalTextureByte(const void* ptr, const void** mapped) {
    return 0;
}

s32 OotPort_AssetRead(void* ram, uintptr_t vrom, size_t size) {
    return 0;
}

s32 OotPort_AssetReadAudio(void* ram, uintptr_t vrom, size_t size) {
    return 0;
}

s32 OotPort_AssetReadAudioUrgent(void* ram, uintptr_t vrom, size_t size) {
    return 0;
}

s32 OotPort_AssetReadHasForegroundPressure(void) {
    return 0;
}
