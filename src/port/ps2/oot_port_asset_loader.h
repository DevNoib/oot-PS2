#ifndef OOT_PORT_ASSET_LOADER_H
#define OOT_PORT_ASSET_LOADER_H

#include "romfile.h"
#include "ultra64.h"

#include <stddef.h>
#include <stdint.h>

#define OOT_PORT_ASSET_READ_FAILED (-1)
#define OOT_PORT_ASSET_READ_OK 0
#define OOT_PORT_ASSET_READ_NOT_EXTERNAL 1
#define OOT_PORT_EXTERNAL_ASSET_NATIVE 1
#define OOT_PORT_EXTERNAL_ASSET_TEXTURE_WORDS 2

typedef struct OotPortExternalAsset {
    uintptr_t vromStart;
    uintptr_t vromEnd;
    uintptr_t originalVromStart;
    uintptr_t originalVromEnd;
    u32 flags;
    uintptr_t fileOffset;
    const char* name;
} OotPortExternalAsset;

typedef struct OotPortExternalAssetTextureRange {
    uintptr_t vromStart;
    uintptr_t vromEnd;
} OotPortExternalAssetTextureRange;

typedef struct OotPortMessageEntry {
    u16 textId;
    u8 typePos;
    u8 pad;
    uintptr_t vromStart;
    uintptr_t vromEnd;
} OotPortMessageEntry;

extern const OotPortExternalAsset gOotPortExternalAssets[];
extern const size_t gOotPortExternalAssetCount;
extern const OotPortExternalAssetTextureRange gOotPortExternalAssetTextureRanges[];
extern const size_t gOotPortExternalAssetTextureRangeCount;
extern const OotPortMessageEntry gOotPortJpnMessageEntries[];
extern const size_t gOotPortJpnMessageEntriesCount;
extern const OotPortMessageEntry gOotPortNesMessageEntries[];
extern const size_t gOotPortNesMessageEntriesCount;
extern const OotPortMessageEntry gOotPortGerMessageEntries[];
extern const size_t gOotPortGerMessageEntriesCount;
extern const OotPortMessageEntry gOotPortFraMessageEntries[];
extern const size_t gOotPortFraMessageEntriesCount;
extern const OotPortMessageEntry gOotPortStaffMessageEntries[];
extern const size_t gOotPortStaffMessageEntriesCount;

s32 OotPort_AssetInit(const char* executablePath);
void OotPort_AssetNotifyResume(void);
const char* OotPort_ResolveRootPath(const char* path, char* buffer, size_t bufferSize);
const char* OotPort_GetAssetRoot(void);
s32 OotPort_SetAssetRoot(const char* root);
s32 OotPort_IsDiscBoot(void);
uintptr_t OotPort_NormalizeVrom(uintptr_t vrom);
void OotPort_NormalizeRomFile(RomFile* file);
s32 OotPort_IsNativeExternalTextureRange(const void* ptr, size_t size);
s32 OotPort_IsLoadedNativeExternalAssetRange(const void* ptr, size_t size);
s32 OotPort_GetLoadedExternalAssetRangeFlags(const void* ptr, size_t size, u32* flags);
s32 OotPort_MarkLoadedExternalAssetRangeFlags(const void* ptr, size_t size, u32 flags);
u32 OotPort_GetExternalAssetRangeSerial(const void* ptr, size_t size);
s32 OotPort_GetNativeExternalTextureMappingRange(const void* ptr, uintptr_t* ramStart, uintptr_t* ramEnd);
s32 OotPort_GetNativeExternalTextureRangeStart(const void* ptr, size_t size, uintptr_t* ramStart);
s32 OotPort_MapNativeExternalTextureByte(const void* ptr, const void** mapped);
s32 OotPort_AssetRead(void* ram, uintptr_t vrom, size_t size);
s32 OotPort_AssetReadAudio(void* ram, uintptr_t vrom, size_t size);
s32 OotPort_AssetReadAudioUrgent(void* ram, uintptr_t vrom, size_t size);
s32 OotPort_AssetReadHasForegroundPressure(void);
const void* OotPort_GetCachedAssetPointer(uintptr_t vrom, size_t size);
const OotPortMessageEntry* OotPort_FindMessageEntry(const OotPortMessageEntry* entries, size_t count, u16 textId);

#endif
