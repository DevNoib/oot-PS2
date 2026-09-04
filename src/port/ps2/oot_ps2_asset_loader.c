#include "oot_port_asset_loader.h"
#include "oot_ps2_asset_setup.h"
#include "oot_port_audio_backend.h"
#include "oot_port_memory.h"
#include "segment_symbols.h"
#include "oot_ps2_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OOT_PS2_NATIVE_ADDR_START           0x00100000U
#define OOT_PS2_NATIVE_ADDR_END             0x02000000U
#define OOT_PS2_LOADED_ASSET_RANGE_COUNT    4096
#define OOT_PS2_ASSET_READ_CHUNK_SIZE       0x4000
#define OOT_PS2_ASSET_CACHE_SIZE            (256 * 1024)
#define OOT_PS2_HOT_ASSET_CACHE_COUNT       4
#define OOT_PS2_HOT_READ_TRACKER_COUNT      16
#define OOT_PS2_HOT_READ_PROMOTE_COUNT      4
#define OOT_PS2_HOT_READ_MAX_SIZE           OOT_PS2_ASSET_CACHE_SIZE
#define OOT_PS2_PACKED_CACHE_BLOCK_SIZE     0x10000
#define OOT_PS2_PACKED_DIRECT_READ_MIN_SIZE (4 * OOT_PS2_PACKED_CACHE_BLOCK_SIZE)
#define OOT_PS2_PACKED_CACHE_TARGET_SIZE    (2 * 1024 * 1024)
#define OOT_PS2_PACKED_CACHE_MIN_SIZE       (1 * 1024 * 1024)
#define OOT_PS2_PACKED_CACHE_ALLOC_STEP     (1024 * 1024)
#define OOT_PS2_PACKED_CACHE_MAX_BLOCK_COUNT \
    (OOT_PS2_PACKED_CACHE_TARGET_SIZE / OOT_PS2_PACKED_CACHE_BLOCK_SIZE)
#define OOT_PS2_PACKED_CACHE_LOOKUP_COUNT     256
#define OOT_PS2_NATIVE_TEXTURE_RANGE_CACHE_COUNT 8
#define OOT_PS2_NATIVE_ASSET_RANGE_CACHE_COUNT   8
#define OOT_PS2_NATIVE_BYTE_RANGE_PAGE_SHIFT     10
#define OOT_PS2_NATIVE_BYTE_RANGE_CACHE_COUNT    256
#define OOT_PS2_ASSET_RANGE_SERIAL_CACHE_SET_COUNT 128
#define OOT_PS2_ASSET_RANGE_SERIAL_CACHE_WAYS      4
#define OOT_PS2_PACKED_ASSET_PATH           "oot_ps2_assets.bin"
#define OOT_PS2_AUDIOBANK_ASSET_NAME        "Audiobank"
#define OOT_PS2_AUDIOSEQ_ASSET_NAME         "Audioseq"
#define OOT_PS2_KANJI_ASSET_NAME            "kanji"
#define OOT_PS2_LINK_ANIMATION_ASSET_NAME   "link_animetion"
#define OOT_PS2_AUDIOTABLE_ASSET_NAME       "Audiotable"
#define OOT_PS2_DO_ACTION_ASSET_NAME         "do_action_static"
#define OOT_PS2_GAMEPLAY_KEEP_ASSET_NAME    "gameplay_keep"
#define OOT_PS2_LINK_BOY_ASSET_NAME         "object_link_boy"
#define OOT_PS2_LINK_CHILD_ASSET_NAME       "object_link_child"
#define OOT_PS2_PAUSE_ICON_ASSET_NAME       "icon_item_static"
#define OOT_PS2_PAUSE_ICON24_ASSET_NAME     "icon_item_24_static"
#define OOT_PS2_PAUSE_FIELD_ASSET_NAME      "icon_item_field_static"
#define OOT_PS2_PAUSE_DUNGEON_ASSET_NAME    "icon_item_dungeon_static"
#define OOT_PS2_PAUSE_GAMEOVER_ASSET_NAME   "icon_item_gameover_static"
#define OOT_PS2_PAUSE_JPN_ASSET_NAME        "icon_item_jpn_static"
#define OOT_PS2_PAUSE_NES_ASSET_NAME        "icon_item_nes_static"
#define OOT_PS2_PAUSE_MAP_NAME_ASSET_NAME   "map_name_static"
#define OOT_PS2_ASSET_READ_ZERO_RETRY_COUNT 16
#define OOT_PS2_ASSET_READ_ZERO_RETRY_USEC  1000
#define OOT_PS2_AUDIO_READ_BACKOFF_USEC     1000
#define OOT_PS2_AUDIO_READ_BACKOFF_MAX_USEC 2000
#define OOT_PS2_ASSET_DMA_COPY_MIN_SIZE     0x1000
#define OOT_PS2_ASSET_DMA_COPY_MAX_SIZE     OOT_PS2_PACKED_CACHE_BLOCK_SIZE
#define OOT_PS2_ASSET_CACHE_LINE_SIZE       64
#define OOT_PS2_CACHE_ALLOCATION_COUNT          128

typedef struct OotPs2LoadedAssetRange {
    uintptr_t ramStart;
    uintptr_t ramEnd;
    uintptr_t assetOffsetStart;
    u32 flags;
    u32 serial;
} OotPs2LoadedAssetRange;

typedef struct OotPs2LoadedAssetSerialRange {
    uintptr_t ramStart;
    uintptr_t ramEnd;
    u32 serial;
    u32 flags;
} OotPs2LoadedAssetSerialRange;

typedef struct OotPs2AssetRangeSerialCacheEntry {
    uintptr_t ramStart;
    uintptr_t ramEnd;
    u32 serial;
    u32 generation;
} OotPs2AssetRangeSerialCacheEntry;

typedef struct OotPs2NativeByteRangeCacheEntry {
    uintptr_t page;
    const OotPs2LoadedAssetRange* range;
    u32 generation;
} OotPs2NativeByteRangeCacheEntry;

typedef struct OotPs2AssetWindowCache {
    const OotPortExternalAsset* asset;
    u8* data;
    size_t capacity;
    size_t offset;
    size_t dataSize;
    u32 lastUsed;
    s32 failed;
    s32 loading;
} OotPs2AssetWindowCache;

typedef struct OotPs2HotReadTracker {
    const OotPortExternalAsset* asset;
    u32 lastUsed;
    u8 hits;
} OotPs2HotReadTracker;

typedef struct OotPs2PackedAssetCacheBlock {
    size_t blockIndex;
    size_t dataSize;
    u32 lastUsed;
    s32 valid;
    s32 loading;
} OotPs2PackedAssetCacheBlock;

typedef struct OotPs2PinnedAssetWindowCache {
    const char* name;
    OotPs2AssetWindowCache cache;
} OotPs2PinnedAssetWindowCache;

typedef struct OotPs2CacheAllocation {
    void* address;
    OotPs2Handle blockId;
    size_t size;
} OotPs2CacheAllocation;

static char sOotPs2AssetRoot[256];
static s32 sOotPs2DiscBoot;
static s32 sOotPs2OriginalRangesSorted = -1;
static OotPs2Handle sOotPs2AssetSema = -1;
static OotPs2Handle sOotPs2PackedAssetFd = -1;
static s32 sOotPs2PackedAssetUnavailable = false;
static size_t sOotPs2PackedAssetSize;
static s32 sOotPs2PackedAssetSizeKnown = false;
static size_t sOotPs2PackedAssetPosition;
static volatile s32 sOotPs2PackedAssetPositionKnown = false;
static volatile s32 sOotPs2AssetResumePending = false;
static u8* sOotPs2PackedCacheData;
static size_t sOotPs2PackedCacheBlockCount;
static s32 sOotPs2PackedCacheInitTried = false;
static OotPs2PackedAssetCacheBlock sOotPs2PackedCacheBlocks[OOT_PS2_PACKED_CACHE_MAX_BLOCK_COUNT];
static OotPs2PackedAssetCacheBlock* sOotPs2PackedCacheLookup[OOT_PS2_PACKED_CACHE_LOOKUP_COUNT];
static OotPs2LoadedAssetRange sOotPs2LoadedAssetRanges[OOT_PS2_LOADED_ASSET_RANGE_COUNT];
static OotPs2LoadedAssetSerialRange sOotPs2LoadedAssetSerialRanges[OOT_PS2_LOADED_ASSET_RANGE_COUNT];
static const OotPs2LoadedAssetRange* sOotPs2NativeTextureRangeCache[OOT_PS2_NATIVE_TEXTURE_RANGE_CACHE_COUNT];
static const OotPs2LoadedAssetRange* sOotPs2LastNativeTextureRange;
static const OotPs2LoadedAssetRange* sOotPs2NativeAssetRangeCache[OOT_PS2_NATIVE_ASSET_RANGE_CACHE_COUNT];
static const OotPs2LoadedAssetRange* sOotPs2LastNativeAssetRange;
static OotPs2NativeByteRangeCacheEntry sOotPs2NativeByteRangeCache[OOT_PS2_NATIVE_BYTE_RANGE_CACHE_COUNT];
static u32 sOotPs2NativeByteRangeCacheGeneration = 1;
static OotPs2AssetRangeSerialCacheEntry sOotPs2AssetRangeSerialCache[OOT_PS2_ASSET_RANGE_SERIAL_CACHE_SET_COUNT]
                                                                [OOT_PS2_ASSET_RANGE_SERIAL_CACHE_WAYS];
static u8 sOotPs2AssetRangeSerialCacheNext[OOT_PS2_ASSET_RANGE_SERIAL_CACHE_SET_COUNT];
static u32 sOotPs2AssetRangeSerialCacheGeneration = 1;
static size_t sOotPs2LoadedAssetSerialRangeCount;
static s32 sOotPs2LoadedAssetSerialRangeIndexComplete = true;
static size_t sOotPs2NativeTextureRangeCacheNext;
static size_t sOotPs2NativeAssetRangeCacheNext;
static size_t sOotPs2LoadedAssetRangeNext;
static size_t sOotPs2LoadedAssetRangeHighWater;

static uintptr_t sOotPs2DodongoFloorStart[2];
static uintptr_t sOotPs2DodongoFloorEnd[2];
static u32 sOotPs2LoadedAssetSerial = 1;
static u32 sOotPs2AssetCacheClock;
static volatile s32 sOotPs2ForegroundAssetReadWaiters;
static volatile s32 sOotPs2ForegroundAssetReadActive;
static OotPs2CacheAllocation sOotPs2CacheAllocations[OOT_PS2_CACHE_ALLOCATION_COUNT];
static OotPs2PinnedAssetWindowCache sOotPs2PinnedAssetCaches[] = {
    { OOT_PS2_LINK_ANIMATION_ASSET_NAME, { NULL, NULL, 0, 0, 0, 0, false, false } },
    { OOT_PS2_DO_ACTION_ASSET_NAME, { NULL, NULL, 0, 0, 0, 0, false, false } },
    { OOT_PS2_GAMEPLAY_KEEP_ASSET_NAME, { NULL, NULL, 0, 0, 0, 0, false, false } },
    { OOT_PS2_LINK_BOY_ASSET_NAME, { NULL, NULL, 0, 0, 0, 0, false, false } },
    { OOT_PS2_LINK_CHILD_ASSET_NAME, { NULL, NULL, 0, 0, 0, 0, false, false } },
    { OOT_PS2_PAUSE_ICON_ASSET_NAME, { NULL, NULL, 0, 0, 0, 0, false, false } },
    { OOT_PS2_PAUSE_ICON24_ASSET_NAME, { NULL, NULL, 0, 0, 0, 0, false, false } },
    { OOT_PS2_PAUSE_FIELD_ASSET_NAME, { NULL, NULL, 0, 0, 0, 0, false, false } },
    { OOT_PS2_PAUSE_DUNGEON_ASSET_NAME, { NULL, NULL, 0, 0, 0, 0, false, false } },
    { OOT_PS2_PAUSE_GAMEOVER_ASSET_NAME, { NULL, NULL, 0, 0, 0, 0, false, false } },
    { OOT_PS2_PAUSE_NES_ASSET_NAME, { NULL, NULL, 0, 0, 0, 0, false, false } },
    { OOT_PS2_PAUSE_MAP_NAME_ASSET_NAME, { NULL, NULL, 0, 0, 0, 0, false, false } },
};
static OotPs2AssetWindowCache sOotPs2HotAssetCaches[OOT_PS2_HOT_ASSET_CACHE_COUNT];
static OotPs2HotReadTracker sOotPs2HotReadTrackers[OOT_PS2_HOT_READ_TRACKER_COUNT];

#define OOT_PS2_PINNED_ASSET_CACHE_COUNT (sizeof(sOotPs2PinnedAssetCaches) / sizeof(sOotPs2PinnedAssetCaches[0]))

static void OotPort_ClearPackedAssetCache(void);
static void OotPort_ClosePackedAssetFile(void);
static void OotPort_PreloadPersistentAssets(void);
static void OotPort_ForgetNativeTextureRangeCache(const OotPs2LoadedAssetRange* range);
static void OotPort_ClearNativeByteRangeCache(void);
static s32 OotPort_NormalizeVromRange(uintptr_t vromStart, uintptr_t vromEnd, uintptr_t* normalizedStart,
                                     uintptr_t* normalizedEnd);

static OotPs2CacheAllocation* OotPort_FindFreeCacheAllocation(void) {
    size_t i;

    for (i = 0; i < OOT_PS2_CACHE_ALLOCATION_COUNT; i++) {
        if (sOotPs2CacheAllocations[i].address == NULL) {
            return &sOotPs2CacheAllocations[i];
        }
    }

    return NULL;
}

static void* OotPort_AllocCacheMemory(size_t size) {
    OotPs2CacheAllocation* allocation = OotPort_FindFreeCacheAllocation();
    void* address;

    if (allocation == NULL) {
        return NULL;
    }

    address = malloc(size);
    if (address == NULL) {
        return NULL;
    }
    allocation->address = address;
    allocation->blockId = -1;
    allocation->size = size;
    return address;
}

static void OotPort_FreeCacheMemory(void* address) {
    size_t i;

    if (address == NULL) {
        return;
    }
    for (i = 0; i < OOT_PS2_CACHE_ALLOCATION_COUNT; i++) {
        OotPs2CacheAllocation* allocation = &sOotPs2CacheAllocations[i];

        if (allocation->address == address) {
            free(address);
            allocation->address = NULL;
            allocation->blockId = -1;
            allocation->size = 0;
            return;
        }
    }
    printf("oot-ps2 ignored invalid cache free address=%p\n", address);
}

static void OotPort_InitAssetSema(void) {
    if (sOotPs2AssetSema >= 0) {
        return;
    }

    sOotPs2AssetSema = OotPs2Sema_Create(1, 1);
    if (sOotPs2AssetSema < 0) {
        printf("oot-ps2 asset sema create failed err=%d\n", (int)sOotPs2AssetSema);
    }
}

static void OotPort_LockAssetLoader(void) {
    if (sOotPs2AssetSema >= 0) {
        OotPs2Sema_Wait(sOotPs2AssetSema);
    }

    if (sOotPs2AssetResumePending) {
        sOotPs2AssetResumePending = false;
        OotPort_ClosePackedAssetFile();
        sOotPs2PackedAssetUnavailable = false;
        printf("oot-ps2 asset file reset after resume\n");
    }
}

static void OotPort_UnlockAssetLoader(void) {
    if (sOotPs2AssetSema >= 0) {
        OotPs2Sema_Signal(sOotPs2AssetSema);
    }
}

static void OotPort_CooperateWithAudioRead(s32 allowAudioYield, volatile s32* currentOffsetKnown) {
    if (!allowAudioYield || !OotPortAudioBackend_NeedsRefillDuringIo()) {
        return;
    }

    if (currentOffsetKnown != NULL) {
        *currentOffsetKnown = false;
    }

    OotPort_UnlockAssetLoader();
    OotPs2Thread_Delay(0);
    OotPort_LockAssetLoader();
}

static size_t OotPort_AssetRangeSerialCacheIndex(uintptr_t ramStart) {
    uintptr_t key = (ramStart >> 4) ^ (ramStart >> 12) ^ (ramStart >> 20);

    return (size_t)(key & (OOT_PS2_ASSET_RANGE_SERIAL_CACHE_SET_COUNT - 1));
}

static void OotPort_ClearAssetRangeSerialCache(void) {
    sOotPs2AssetRangeSerialCacheGeneration++;
    if (sOotPs2AssetRangeSerialCacheGeneration == 0) {
        memset(sOotPs2AssetRangeSerialCache, 0, sizeof(sOotPs2AssetRangeSerialCache));
        sOotPs2AssetRangeSerialCacheGeneration = 1;
    }
}

static s32 OotPort_GetCachedAssetRangeSerial(uintptr_t ramStart, uintptr_t ramEnd, u32* serial) {
    const OotPs2AssetRangeSerialCacheEntry* set =
        sOotPs2AssetRangeSerialCache[OotPort_AssetRangeSerialCacheIndex(ramStart)];
    size_t way;

    for (way = 0; way < OOT_PS2_ASSET_RANGE_SERIAL_CACHE_WAYS; way++) {
        const OotPs2AssetRangeSerialCacheEntry* entry = &set[way];

        if ((entry->generation == sOotPs2AssetRangeSerialCacheGeneration) && (entry->ramStart == ramStart) &&
            (ramEnd <= entry->ramEnd)) {
            *serial = entry->serial;
            return true;
        }
    }

    return false;
}

static void OotPort_RememberAssetRangeSerial(uintptr_t ramStart, uintptr_t ramEnd, u32 serial) {
    size_t setIndex = OotPort_AssetRangeSerialCacheIndex(ramStart);
    size_t way = sOotPs2AssetRangeSerialCacheNext[setIndex];
    OotPs2AssetRangeSerialCacheEntry* entry = &sOotPs2AssetRangeSerialCache[setIndex][way];

    entry->ramStart = ramStart;
    entry->ramEnd = ramEnd;
    entry->serial = serial;
    entry->generation = sOotPs2AssetRangeSerialCacheGeneration;
    sOotPs2AssetRangeSerialCacheNext[setIndex] =
        (way + 1) & (OOT_PS2_ASSET_RANGE_SERIAL_CACHE_WAYS - 1);
}

static s32 OotPort_PathHasPrefix(const char* path, const char* prefix) {
    size_t i;

    if ((path == NULL) || (prefix == NULL)) {
        return false;
    }
    for (i = 0; prefix[i] != '\0'; i++) {
        char a = path[i];
        char b = prefix[i];

        if ((a >= 'A') && (a <= 'Z')) {
            a = (char)(a - 'A' + 'a');
        }
        if ((b >= 'A') && (b <= 'Z')) {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

const char* OotPort_GetAssetRoot(void) {
    return sOotPs2AssetRoot;
}

s32 OotPort_SetAssetRoot(const char* root) {
    size_t length;

    if ((root == NULL) || (root[0] == '\0')) {
        return false;
    }
    length = strlen(root);
    if (length + 2 > sizeof(sOotPs2AssetRoot)) {
        return false;
    }
    memcpy(sOotPs2AssetRoot, root, length);
    if ((root[length - 1] != '/') && (root[length - 1] != '\\')) {
        sOotPs2AssetRoot[length++] = '/';
    }
    sOotPs2AssetRoot[length] = '\0';
    return true;
}

s32 OotPort_IsDiscBoot(void) {
    return sOotPs2DiscBoot;
}

s32 OotPort_AssetInit(const char* executablePath) {
    const char* slash;
    const char* backslash;
    size_t length;
    char root[256];

    OotPort_ClosePackedAssetFile();
    OotPort_ClearPackedAssetCache();
    sOotPs2PackedAssetUnavailable = false;
    sOotPs2AssetRoot[0] = '\0';
    sOotPs2DiscBoot = false;

    if ((executablePath == NULL) || (executablePath[0] == '\0')) {
        printf("oot-ps2 asset root missing argv0\n");
        return false;
    }

    sOotPs2DiscBoot = OotPort_PathHasPrefix(executablePath, "cdrom0:") ||
                       OotPort_PathHasPrefix(executablePath, "cdrom:") ||
                       OotPort_PathHasPrefix(executablePath, "cdfs:");
    slash = strrchr(executablePath, '/');
    backslash = strrchr(executablePath, '\\');
    if ((backslash != NULL) && ((slash == NULL) || (backslash > slash))) {
        slash = backslash;
    }

    if (slash == NULL) {
        const char* colon = strchr(executablePath, ':');

        if (colon == NULL) {
            printf("oot-ps2 asset root no directory argv0=%s\n", executablePath);
            return false;
        }
        length = (size_t)(colon - executablePath) + 1;
    } else {
        length = (size_t)(slash - executablePath) + 1;
    }
    if (length >= sizeof(root)) {
        return false;
    }
    memcpy(root, executablePath, length);
    root[length] = '\0';
    if (!OotPort_SetAssetRoot(root)) {
        return false;
    }

    printf("oot-ps2 asset root=%s\n", sOotPs2AssetRoot);
    if (!OotPs2AssetSetup_Ensure()) {
        sOotPs2PackedAssetUnavailable = true;
        return false;
    }
    OotPort_InitAssetSema();
    OotPort_PreloadPersistentAssets();
    return true;
}

void OotPort_AssetNotifyResume(void) {

    sOotPs2PackedAssetPositionKnown = false;
    sOotPs2AssetResumePending = true;
}

static s32 OotPort_IsAbsolutePath(const char* path) {
    const char* slash;
    const char* colon;

    if ((path == NULL) || (path[0] == '\0')) {
        return false;
    }

    if ((path[0] == '/') || (path[0] == '\\')) {
        return true;
    }

    colon = strchr(path, ':');
    slash = strpbrk(path, "/\\");
    return (colon != NULL) && ((slash == NULL) || (colon < slash));
}

const char* OotPort_ResolveRootPath(const char* path, char* buffer, size_t bufferSize) {
    int written;

    if (OotPort_IsAbsolutePath(path) || (sOotPs2AssetRoot[0] == '\0')) {
        return path;
    }

    written = snprintf(buffer, bufferSize, "%s%s", sOotPs2AssetRoot, path);
    if ((written < 0) || ((size_t)written >= bufferSize)) {
        printf("oot-ps2 root path too long root=%s path=%s\n", sOotPs2AssetRoot, path);
        return path;
    }

    return buffer;
}

static void OotPort_ClearPackedAssetCache(void) {
    memset(sOotPs2PackedCacheBlocks, 0, sizeof(sOotPs2PackedCacheBlocks));
    memset(sOotPs2PackedCacheLookup, 0, sizeof(sOotPs2PackedCacheLookup));
}

static void OotPort_InitPackedAssetCache(void) {
    size_t cacheSize;

    if (sOotPs2PackedCacheInitTried) {
        return;
    }

    sOotPs2PackedCacheInitTried = true;
    for (cacheSize = OOT_PS2_PACKED_CACHE_TARGET_SIZE; cacheSize >= OOT_PS2_PACKED_CACHE_MIN_SIZE;) {
        sOotPs2PackedCacheData = OotPort_AllocCacheMemory(cacheSize);
        if (sOotPs2PackedCacheData != NULL) {
            sOotPs2PackedCacheBlockCount = cacheSize / OOT_PS2_PACKED_CACHE_BLOCK_SIZE;
            OotPort_ClearPackedAssetCache();
            printf("oot-ps2 packed asset cache size=%lu blocks=%lu\n", (unsigned long)cacheSize,
                   (unsigned long)sOotPs2PackedCacheBlockCount);
            return;
        }

        if (cacheSize == OOT_PS2_PACKED_CACHE_MIN_SIZE) {
            break;
        }

        if (cacheSize > (OOT_PS2_PACKED_CACHE_MIN_SIZE + OOT_PS2_PACKED_CACHE_ALLOC_STEP)) {
            cacheSize -= OOT_PS2_PACKED_CACHE_ALLOC_STEP;
        } else {
            cacheSize = OOT_PS2_PACKED_CACHE_MIN_SIZE;
        }
    }

    printf("oot-ps2 packed asset cache disabled alloc failed\n");
}

static void OotPort_ClosePackedAssetFile(void) {
    if (sOotPs2PackedAssetFd >= 0) {
        OotPs2File_Close(sOotPs2PackedAssetFd);
        sOotPs2PackedAssetFd = -1;
    }

    sOotPs2PackedAssetSize = 0;
    sOotPs2PackedAssetSizeKnown = false;
    sOotPs2PackedAssetPosition = 0;
    sOotPs2PackedAssetPositionKnown = false;
}

static OotPs2Handle OotPort_OpenPackedAssetFile(const char** resolvedPath, char* pathBuffer, size_t pathBufferSize) {
    *resolvedPath = OotPort_ResolveRootPath(OOT_PS2_PACKED_ASSET_PATH, pathBuffer, pathBufferSize);

    if (sOotPs2PackedAssetFd >= 0) {
        return sOotPs2PackedAssetFd;
    }

    if (sOotPs2PackedAssetUnavailable) {
        return -1;
    }

    sOotPs2PackedAssetFd = OotPs2File_Open(*resolvedPath, OOT_PS2_FILE_RDONLY, 0);
    if (sOotPs2PackedAssetFd < 0) {
        printf("oot-ps2 packed asset open failed path=%s err=%d\n", *resolvedPath, (int)sOotPs2PackedAssetFd);
        sOotPs2PackedAssetUnavailable = true;
        sOotPs2PackedAssetFd = -1;
        return -1;
    }

    if (!sOotPs2PackedAssetSizeKnown) {
        OotPs2Offset end = OotPs2File_Seek(sOotPs2PackedAssetFd, 0, OOT_PS2_SEEK_END);

        if (end >= 0) {
            sOotPs2PackedAssetSize = (size_t)end;
            sOotPs2PackedAssetSizeKnown = true;
            OotPs2File_Seek(sOotPs2PackedAssetFd, 0, OOT_PS2_SEEK_SET);
            sOotPs2PackedAssetPosition = 0;
            sOotPs2PackedAssetPositionKnown = true;
        } else {
            printf("oot-ps2 packed asset size query failed path=%s err=%d\n", *resolvedPath, (int)end);
            sOotPs2PackedAssetPositionKnown = false;
        }
    }

    return sOotPs2PackedAssetFd;
}
static int OotPort_ReadAssetChunk(OotPs2Handle fd, u8* out, int chunk, const char* path, size_t readOffset) {
    int zeroReads = 0;

    while (true) {
        int read = OotPs2File_Read(fd, out, chunk);

        if (read != 0) {
            return read;
        }

        if (zeroReads >= OOT_PS2_ASSET_READ_ZERO_RETRY_COUNT) {
            printf("oot-ps2 asset read zero path=%s off=%lu size=%d retries=%d\n", path,
                   (unsigned long)readOffset, chunk, zeroReads);
            return read;
        }

        zeroReads++;
        OotPs2Thread_Delay(OOT_PS2_ASSET_READ_ZERO_RETRY_USEC);
    }
}

static u32 OotPort_NextAssetCacheClock(void) {
    sOotPs2AssetCacheClock++;
    if (sOotPs2AssetCacheClock == 0) {
        sOotPs2AssetCacheClock = 1;
    }
    return sOotPs2AssetCacheClock;
}

static const char* OotPort_AssetName(const OotPortExternalAsset* asset) {
    if ((asset == NULL) || (asset->name == NULL)) {
        return "<unknown>";
    }

    return asset->name;
}

static s32 OotPort_AssetNameEquals(const OotPortExternalAsset* asset, const char* expectedName) {
    if (expectedName == NULL) {
        return false;
    }

    return strcmp(OotPort_AssetName(asset), expectedName) == 0;
}

static OotPs2AssetWindowCache* OotPort_FindPinnedAssetCache(const OotPortExternalAsset* asset) {
    size_t i;

    if (asset == NULL) {
        return NULL;
    }

    for (i = 0; i < OOT_PS2_PINNED_ASSET_CACHE_COUNT; i++) {
        OotPs2PinnedAssetWindowCache* pinned = &sOotPs2PinnedAssetCaches[i];
        OotPs2AssetWindowCache* cache = &pinned->cache;

        if (OotPort_AssetNameEquals(asset, pinned->name)) {
            if (cache->loading) {
                return NULL;
            }
            cache->asset = asset;
            return cache;
        }
    }

    return NULL;
}

static OotPs2AssetWindowCache* OotPort_FindLoadedHotAssetCache(const OotPortExternalAsset* asset) {
    size_t i;

    for (i = 0; i < OOT_PS2_HOT_ASSET_CACHE_COUNT; i++) {
        OotPs2AssetWindowCache* cache = &sOotPs2HotAssetCaches[i];

        if ((cache->asset == asset) && (cache->data != NULL) && !cache->loading) {
            return cache;
        }
    }

    return NULL;
}

static OotPs2AssetWindowCache* OotPort_FindAssetCache(const OotPortExternalAsset* asset) {
    OotPs2AssetWindowCache* cache = OotPort_FindPinnedAssetCache(asset);

    if (cache != NULL) {
        return cache;
    }

    return OotPort_FindLoadedHotAssetCache(asset);
}

static s32 OotPort_IsAudioAsset(const OotPortExternalAsset* asset) {
    return (asset != NULL) &&
           (OotPort_AssetNameEquals(asset, OOT_PS2_AUDIOBANK_ASSET_NAME) ||
            OotPort_AssetNameEquals(asset, OOT_PS2_AUDIOSEQ_ASSET_NAME) ||
            OotPort_AssetNameEquals(asset, OOT_PS2_AUDIOTABLE_ASSET_NAME));
}

static s32 OotPort_IsNonActiveLanguageMessageAsset(const OotPortExternalAsset* asset) {
    return (asset != NULL) &&
           (OotPort_AssetNameEquals(asset, "jpn_message_data_static") ||
            OotPort_AssetNameEquals(asset, "ger_message_data_static") ||
            OotPort_AssetNameEquals(asset, "fra_message_data_static"));
}

static s32 OotPort_RecordHotAssetRead(const OotPortExternalAsset* asset) {
    OotPs2HotReadTracker* best = NULL;
    size_t i;

    if ((asset == NULL) || OotPort_IsAudioAsset(asset) || OotPort_IsNonActiveLanguageMessageAsset(asset)) {
        return false;
    }

    for (i = 0; i < OOT_PS2_HOT_READ_TRACKER_COUNT; i++) {
        OotPs2HotReadTracker* tracker = &sOotPs2HotReadTrackers[i];

        if (tracker->asset == asset) {
            if (tracker->hits < 0xFF) {
                tracker->hits++;
            }
            tracker->lastUsed = OotPort_NextAssetCacheClock();
            return tracker->hits >= OOT_PS2_HOT_READ_PROMOTE_COUNT;
        }

        if (tracker->asset == NULL) {
            best = tracker;
        } else if ((best == NULL) || (tracker->lastUsed < best->lastUsed)) {
            best = tracker;
        }
    }

    if (best == NULL) {
        return false;
    }

    best->asset = asset;
    best->hits = 1;
    best->lastUsed = OotPort_NextAssetCacheClock();
    return false;
}

static OotPs2AssetWindowCache* OotPort_GetHotAssetCacheCandidate(const OotPortExternalAsset* asset) {
    OotPs2AssetWindowCache* best = NULL;
    size_t i;

    if (asset == NULL) {
        return NULL;
    }

    for (i = 0; i < OOT_PS2_HOT_ASSET_CACHE_COUNT; i++) {
        OotPs2AssetWindowCache* cache = &sOotPs2HotAssetCaches[i];

        if (cache->loading) {
            continue;
        }

        if (cache->asset == asset) {
            return cache;
        }

        if (cache->asset == NULL) {
            best = cache;
        } else if ((best == NULL) || (cache->lastUsed < best->lastUsed)) {
            best = cache;
        }
    }

    if (best == NULL) {
        return NULL;
    }

    if ((best->asset != NULL) && (best->asset != asset)) {
        OotPort_FreeCacheMemory(best->data);
        best->data = NULL;
        best->capacity = 0;
        best->offset = 0;
        best->dataSize = 0;
        best->failed = false;
    }

    best->asset = asset;
    best->lastUsed = OotPort_NextAssetCacheClock();
    return best;
}

static s32 OotPort_ReadPackedOpenFileRange(OotPs2Handle fd, const char* path, size_t offset, u8* out, size_t size,
                                          size_t maxReadChunk, s32 allowAudioYield) {
    size_t remaining = size;
    size_t readOffset = offset;

    if (offset > 0x7FFFFFFFUL) {
        printf("oot-ps2 asset seek offset too large path=%s off=%lu\n", path, (unsigned long)offset);
        return false;
    }

    while (remaining != 0) {
        int chunk;
        int read;

        if (!sOotPs2PackedAssetPositionKnown || (sOotPs2PackedAssetPosition != readOffset)) {
            if (OotPs2File_Seek(fd, (int)readOffset, OOT_PS2_SEEK_SET) < 0) {
                sOotPs2PackedAssetPositionKnown = false;
                printf("oot-ps2 asset seek failed path=%s off=%lu\n", path, (unsigned long)readOffset);
                return false;
            }
            sOotPs2PackedAssetPosition = readOffset;
            sOotPs2PackedAssetPositionKnown = true;
        }

        chunk = remaining > maxReadChunk ? (int)maxReadChunk : (int)remaining;
        read = OotPort_ReadAssetChunk(fd, out, chunk, path, readOffset);

        if (read <= 0) {
            sOotPs2PackedAssetPositionKnown = false;
            printf("oot-ps2 asset read failed path=%s off=%lu size=%lu read=%d\n", path,
                   (unsigned long)readOffset, (unsigned long)remaining, read);
            return false;
        }

        out += read;
        readOffset += read;
        remaining -= read;
        sOotPs2PackedAssetPosition += read;
        sOotPs2PackedAssetPositionKnown = true;

        if (remaining != 0) {
            OotPort_CooperateWithAudioRead(allowAudioYield, &sOotPs2PackedAssetPositionKnown);
        }
    }

    return true;
}

static u8* OotPort_PackedCacheBlockData(size_t slot) {
    return &sOotPs2PackedCacheData[slot * OOT_PS2_PACKED_CACHE_BLOCK_SIZE];
}

static OotPs2PackedAssetCacheBlock* OotPort_FindPackedCacheBlock(size_t blockIndex) {
    const size_t lookupIndex = blockIndex & (OOT_PS2_PACKED_CACHE_LOOKUP_COUNT - 1);
    OotPs2PackedAssetCacheBlock* block = sOotPs2PackedCacheLookup[lookupIndex];
    size_t i;

    if ((block != NULL) && block->valid && !block->loading && (block->blockIndex == blockIndex)) {
        block->lastUsed = OotPort_NextAssetCacheClock();
        return block;
    }

    for (i = 0; i < sOotPs2PackedCacheBlockCount; i++) {
        block = &sOotPs2PackedCacheBlocks[i];

        if (block->valid && !block->loading && (block->blockIndex == blockIndex)) {
            block->lastUsed = OotPort_NextAssetCacheClock();
            sOotPs2PackedCacheLookup[lookupIndex] = block;
            return block;
        }
    }

    return NULL;
}

static OotPs2PackedAssetCacheBlock* OotPort_SelectPackedCacheBlock(void) {
    OotPs2PackedAssetCacheBlock* best = NULL;
    size_t i;

    for (i = 0; i < sOotPs2PackedCacheBlockCount; i++) {
        OotPs2PackedAssetCacheBlock* block = &sOotPs2PackedCacheBlocks[i];

        if (block->loading) {
            continue;
        }

        if (!block->valid) {
            return block;
        }

        if ((best == NULL) || (block->lastUsed < best->lastUsed)) {
            best = block;
        }
    }

    return best;
}

static OotPs2PackedAssetCacheBlock* OotPort_LoadPackedCacheBlock(OotPs2Handle fd, const char* path, size_t blockIndex) {
    OotPs2PackedAssetCacheBlock* block;
    size_t blockStart = blockIndex * OOT_PS2_PACKED_CACHE_BLOCK_SIZE;
    size_t readSize = OOT_PS2_PACKED_CACHE_BLOCK_SIZE;
    size_t slot;

    if (sOotPs2PackedCacheData == NULL) {
        OotPort_InitPackedAssetCache();
        if (sOotPs2PackedCacheData == NULL) {
            return NULL;
        }
    }

    block = OotPort_FindPackedCacheBlock(blockIndex);
    if (block != NULL) {
        return block;
    }

    if (sOotPs2PackedAssetSizeKnown) {
        if (blockStart >= sOotPs2PackedAssetSize) {
            return NULL;
        }
        if (readSize > (sOotPs2PackedAssetSize - blockStart)) {
            readSize = sOotPs2PackedAssetSize - blockStart;
        }
    }

    block = OotPort_SelectPackedCacheBlock();
    if (block == NULL) {
        return NULL;
    }

    slot = (size_t)(block - sOotPs2PackedCacheBlocks);
    if (block->valid) {
        size_t oldLookupIndex = block->blockIndex & (OOT_PS2_PACKED_CACHE_LOOKUP_COUNT - 1);

        if (sOotPs2PackedCacheLookup[oldLookupIndex] == block) {
            sOotPs2PackedCacheLookup[oldLookupIndex] = NULL;
        }
    }

    block->valid = false;
    block->loading = true;
    block->blockIndex = blockIndex;
    block->dataSize = 0;
    if (!OotPort_ReadPackedOpenFileRange(fd, path, blockStart, OotPort_PackedCacheBlockData(slot), readSize,
                                        OOT_PS2_PACKED_CACHE_BLOCK_SIZE, false)) {
        block->loading = false;
        block->valid = false;
        return NULL;
    }

    block->dataSize = readSize;
    block->lastUsed = OotPort_NextAssetCacheClock();
    block->loading = false;
    block->valid = true;
    sOotPs2PackedCacheLookup[blockIndex & (OOT_PS2_PACKED_CACHE_LOOKUP_COUNT - 1)] = block;
    return block;
}

static void OotPort_WritebackCacheRange(const void* address, size_t size) {
    uintptr_t start;
    uintptr_t end;

    if ((address == NULL) || (size == 0)) {
        return;
    }

    start = (uintptr_t)address & ~(OOT_PS2_ASSET_CACHE_LINE_SIZE - 1);
    end = ((uintptr_t)address + size + OOT_PS2_ASSET_CACHE_LINE_SIZE - 1) &
          ~(OOT_PS2_ASSET_CACHE_LINE_SIZE - 1);
    OotPs2Cache_WritebackRange((void*)start, end - start);
}

static s32 OotPs2_TryFastAssetCopy(void* dst, const void* src, size_t size) {
    if ((size < OOT_PS2_ASSET_DMA_COPY_MIN_SIZE) ||
        (size > OOT_PS2_ASSET_DMA_COPY_MAX_SIZE) ||
        ((((uintptr_t)dst | (uintptr_t)src | size) & 0xF) != 0)) {
        return false;
    }

    OotPort_MemcpyFast(dst, src, size);
    return true;
}

static void OotPort_CopyAssetBytes(void* dst, const void* src, size_t size, s32 useFastCopy) {

    if (useFastCopy && !OotPortAudioBackend_NeedsRefillUrgently() && OotPs2_TryFastAssetCopy(dst, src, size)) {
        return;
    }

    if (useFastCopy) {
        OotPort_MemcpyFast(dst, src, size);
    } else {
        memcpy(dst, src, size);
    }
}

static s32 OotPort_ReadPackedAssetFileRangeCached(OotPs2Handle fd, const char* path, size_t offset, u8* out, size_t size,
                                                 s32 useFastCopy, s32 allowAudioYield) {
    size_t remaining = size;
    size_t cursor = offset;

    while (remaining != 0) {
        size_t blockIndex = cursor / OOT_PS2_PACKED_CACHE_BLOCK_SIZE;
        size_t blockOffset = cursor & (OOT_PS2_PACKED_CACHE_BLOCK_SIZE - 1);
        OotPs2PackedAssetCacheBlock* block = OotPort_LoadPackedCacheBlock(fd, path, blockIndex);
        size_t slot;
        size_t available;
        size_t copySize;

        if ((block == NULL) || (blockOffset >= block->dataSize)) {
            return false;
        }

        slot = (size_t)(block - sOotPs2PackedCacheBlocks);
        available = block->dataSize - blockOffset;
        copySize = remaining < available ? remaining : available;
        OotPort_CopyAssetBytes(out, &OotPort_PackedCacheBlockData(slot)[blockOffset], copySize, useFastCopy);

        out += copySize;
        cursor += copySize;
        remaining -= copySize;

        if (remaining != 0) {
            OotPort_CooperateWithAudioRead(allowAudioYield, &sOotPs2PackedAssetPositionKnown);
        }
    }

    return true;
}

static s32 OotPort_ReadPackedAssetFileRange(const OotPortExternalAsset* asset, size_t offset, u8* out, size_t size,
                                           s32 useBlockCache, s32 useFastCopy, s32 allowAudioYield) {
    char pathBuffer[384];
    const char* path;
    OotPs2Handle fd;
    size_t packedOffset;
    size_t maxReadChunk;

    if ((asset == NULL) || (asset->fileOffset > (UINTPTR_MAX - offset))) {
        return false;
    }

    fd = OotPort_OpenPackedAssetFile(&path, pathBuffer, sizeof(pathBuffer));
    if (fd < 0) {
        return false;
    }

    packedOffset = asset->fileOffset + offset;
    if (useBlockCache &&
        OotPort_ReadPackedAssetFileRangeCached(fd, path, packedOffset, out, size, useFastCopy, allowAudioYield)) {
        return true;
    }

    maxReadChunk = (size >= OOT_PS2_PACKED_DIRECT_READ_MIN_SIZE)
                       ? OOT_PS2_PACKED_CACHE_BLOCK_SIZE
                       : (allowAudioYield ? OOT_PS2_ASSET_READ_CHUNK_SIZE
                                          : OOT_PS2_PACKED_CACHE_BLOCK_SIZE);
    return OotPort_ReadPackedOpenFileRange(fd, path, packedOffset, out, size, maxReadChunk, allowAudioYield);
}

static size_t OotPort_GetAssetCacheSizeLimit(const OotPortExternalAsset* asset) {
    size_t i;

    if (asset == NULL) {
        return OOT_PS2_ASSET_CACHE_SIZE;
    }

    for (i = 0; i < OOT_PS2_PINNED_ASSET_CACHE_COUNT; i++) {
        if (OotPort_AssetNameEquals(asset, sOotPs2PinnedAssetCaches[i].name)) {
            if (asset->vromEnd > asset->vromStart) {
                return asset->vromEnd - asset->vromStart;
            }
            return 0;
        }
    }

    return OOT_PS2_ASSET_CACHE_SIZE;
}

static s32 OotPort_EnsureAssetCacheRange(OotPs2AssetWindowCache* cache, const OotPortExternalAsset* asset, size_t offset,
                                        size_t size, s32 allowAudioYield) {
    u8* data;
    size_t fileSize;
    size_t cacheOffset;
    size_t windowStart;
    size_t windowSize;
    size_t capacity;
    size_t cacheSizeLimit;

    if (cache == NULL) {
        return false;
    }

    if (cache->loading) {
        return false;
    }

    cache->asset = asset;
    cache->lastUsed = OotPort_NextAssetCacheClock();

    cacheSizeLimit = OotPort_GetAssetCacheSizeLimit(asset);
    if (size > cacheSizeLimit) {
        return false;
    }

    if ((cache->data != NULL) && (offset >= cache->offset)) {
        cacheOffset = offset - cache->offset;
        if ((cacheOffset <= cache->dataSize) && (size <= (cache->dataSize - cacheOffset))) {
            return true;
        }
    }

    if (cache->failed) {
        return false;
    }

    if (asset->vromEnd <= asset->vromStart) {
        cache->failed = true;
        return false;
    }

    fileSize = asset->vromEnd - asset->vromStart;
    if ((offset > fileSize) || (size > (fileSize - offset))) {
        return false;
    }

    if (cache->data == NULL) {
        capacity = fileSize < cacheSizeLimit ? fileSize : cacheSizeLimit;
        if (size > capacity) {
            return false;
        }

        data = OotPort_AllocCacheMemory(capacity);
        if (data == NULL) {
            printf("oot-ps2 asset cache alloc failed name=%s size=%lu\n", OotPort_AssetName(asset),
                   (unsigned long)capacity);
            cache->failed = true;
            return false;
        }

        cache->data = data;
        cache->capacity = capacity;
    }

    if (fileSize <= cache->capacity) {
        windowStart = 0;
        windowSize = fileSize;
    } else {
        windowStart = (offset / cache->capacity) * cache->capacity;
        cacheOffset = offset - windowStart;
        if (size > (cache->capacity - cacheOffset)) {
            windowStart = (offset + size) - cache->capacity;
        }
        if (windowStart > (fileSize - cache->capacity)) {
            windowStart = fileSize - cache->capacity;
        }
        windowSize = cache->capacity;
    }

    cache->loading = true;
    if (!OotPort_ReadPackedAssetFileRange(asset, windowStart, cache->data, windowSize, false, false,
                                         allowAudioYield)) {
        cache->loading = false;
        OotPort_FreeCacheMemory(cache->data);
        cache->data = NULL;
        cache->capacity = 0;
        cache->offset = 0;
        cache->dataSize = 0;
        cache->failed = true;
        return false;
    }

    cache->offset = windowStart;
    cache->dataSize = windowSize;
    cache->loading = false;
    return true;
}

static void OotPort_PreloadPersistentAssets(void) {
    s32 preloaded[OOT_PS2_PINNED_ASSET_CACHE_COUNT] = { false };
    size_t preloadIndex;

    for (preloadIndex = 0; preloadIndex < OOT_PS2_PINNED_ASSET_CACHE_COUNT; preloadIndex++) {
        const OotPortExternalAsset* selectedAsset = NULL;
        OotPs2PinnedAssetWindowCache* selectedPinned = NULL;
        size_t selectedCacheIndex = 0;
        size_t selectedFileSize = 0;
        size_t cacheIndex;

        for (cacheIndex = 0; cacheIndex < OOT_PS2_PINNED_ASSET_CACHE_COUNT; cacheIndex++) {
            OotPs2PinnedAssetWindowCache* pinned = &sOotPs2PinnedAssetCaches[cacheIndex];
            size_t assetIndex;

            if (preloaded[cacheIndex]) {
                continue;
            }

            for (assetIndex = 0; assetIndex < gOotPortExternalAssetCount; assetIndex++) {
                const OotPortExternalAsset* asset = &gOotPortExternalAssets[assetIndex];
                size_t fileSize;

                if (!OotPort_AssetNameEquals(asset, pinned->name)) {
                    continue;
                }

                fileSize = asset->vromEnd - asset->vromStart;
                if ((selectedAsset == NULL) || (fileSize > selectedFileSize)) {
                    selectedAsset = asset;
                    selectedPinned = pinned;
                    selectedCacheIndex = cacheIndex;
                    selectedFileSize = fileSize;
                }
                break;
            }
        }

        if (selectedAsset == NULL) {
            break;
        }

        preloaded[selectedCacheIndex] = true;
        selectedPinned->cache.asset = selectedAsset;
        if (selectedFileSize > OotPort_GetAssetCacheSizeLimit(selectedAsset)) {
            printf("oot-ps2 persistent asset deferred cache name=%s size=%lu window=%lu\n",
                   OotPort_AssetName(selectedAsset), (unsigned long)selectedFileSize,
                   (unsigned long)OotPort_GetAssetCacheSizeLimit(selectedAsset));
            continue;
        }

        if (!OotPort_EnsureAssetCacheRange(&selectedPinned->cache, selectedAsset, 0, selectedFileSize, false)) {
            printf("oot-ps2 persistent asset preload failed name=%s size=%lu\n", OotPort_AssetName(selectedAsset),
                   (unsigned long)selectedFileSize);
        }
    }
}

static const OotPortExternalAsset* OotPort_FindContainingExternalAsset(uintptr_t vrom, size_t* index) {
    size_t left = 0;
    size_t right = gOotPortExternalAssetCount;

    while (left < right) {
        size_t mid = left + ((right - left) / 2);
        const OotPortExternalAsset* asset = &gOotPortExternalAssets[mid];

        if (vrom < asset->vromStart) {
            right = mid;
        } else if (vrom >= asset->vromEnd) {
            left = mid + 1;
        } else {
            if (index != NULL) {
                *index = mid;
            }
            return asset;
        }
    }

    return NULL;
}

static s32 OotPort_IsExternalAssetSpanContiguous(size_t index, uintptr_t vromStart, uintptr_t vromEnd) {
    uintptr_t cursor = vromStart;

    if (vromEnd < vromStart) {
        return false;
    }

    if (vromStart == vromEnd) {
        return true;
    }

    while (cursor < vromEnd) {
        const OotPortExternalAsset* asset;

        if (index >= gOotPortExternalAssetCount) {
            return false;
        }

        asset = &gOotPortExternalAssets[index];
        if ((cursor < asset->vromStart) || (cursor >= asset->vromEnd)) {
            return false;
        }

        cursor = asset->vromEnd;
        if ((cursor < vromEnd) &&
            (((index + 1) >= gOotPortExternalAssetCount) || (gOotPortExternalAssets[index + 1].vromStart != cursor))) {
            return false;
        }

        index++;
    }

    return true;
}

const void* OotPort_GetCachedAssetPointer(uintptr_t vrom, size_t size) {
    uintptr_t normalizedVrom;
    uintptr_t normalizedEnd;
    const OotPortExternalAsset* asset;
    OotPs2AssetWindowCache* cache;
    const void* ptr = NULL;
    size_t offset;
    size_t cacheOffset;

    if ((vrom > (UINTPTR_MAX - size)) || !OotPort_NormalizeVromRange(vrom, vrom + size, &normalizedVrom,
                                                                    &normalizedEnd)) {
        return NULL;
    }

    asset = OotPort_FindContainingExternalAsset(normalizedVrom, NULL);
    if ((asset == NULL) || (normalizedEnd > asset->vromEnd)) {
        return NULL;
    }

    OotPort_InitAssetSema();
    OotPort_LockAssetLoader();
    cache = OotPort_FindPinnedAssetCache(asset);
    if ((cache != NULL) && (cache->data != NULL) && !cache->loading && (normalizedVrom >= asset->vromStart)) {
        offset = normalizedVrom - asset->vromStart;
        if (offset >= cache->offset) {
            cacheOffset = offset - cache->offset;
            if ((cacheOffset <= cache->dataSize) && (size <= (cache->dataSize - cacheOffset))) {
                cache->lastUsed = OotPort_NextAssetCacheClock();
                ptr = &cache->data[cacheOffset];
            }
        }
    }
    OotPort_UnlockAssetLoader();
    return ptr;
}

static s32 OotPort_RangeContains(uintptr_t rangeStart, uintptr_t rangeEnd, uintptr_t vromStart, uintptr_t vromEnd) {
    if ((rangeEnd < rangeStart) || (vromEnd < vromStart)) {
        return false;
    }

    if (vromStart == vromEnd) {
        return (vromStart >= rangeStart) && (vromStart <= rangeEnd);
    }

    return (vromStart >= rangeStart) && (vromStart < rangeEnd) && (vromEnd <= rangeEnd);
}

static s32 OotPort_AreOriginalAssetRangesSorted(void) {
    size_t i;
    uintptr_t previousStart = 0;

    if (sOotPs2OriginalRangesSorted >= 0) {
        return sOotPs2OriginalRangesSorted;
    }

    sOotPs2OriginalRangesSorted = true;
    for (i = 0; i < gOotPortExternalAssetCount; i++) {
        const OotPortExternalAsset* asset = &gOotPortExternalAssets[i];

        if (asset->originalVromEnd <= asset->originalVromStart) {
            continue;
        }

        if ((i != 0) && (asset->originalVromStart < previousStart)) {
            sOotPs2OriginalRangesSorted = false;
            break;
        }

        previousStart = asset->originalVromStart;
    }

    return sOotPs2OriginalRangesSorted;
}

static const OotPortExternalAsset* OotPort_FindContainingOriginalExternalAssetRange(uintptr_t vromStart,
                                                                                  uintptr_t vromEnd) {
    size_t i;

    if (OotPort_AreOriginalAssetRangesSorted()) {
        size_t left = 0;
        size_t right = gOotPortExternalAssetCount;

        while (left < right) {
            size_t mid = left + ((right - left) / 2);
            const OotPortExternalAsset* asset = &gOotPortExternalAssets[mid];

            if (vromStart < asset->originalVromStart) {
                right = mid;
            } else if ((vromStart > asset->originalVromEnd) ||
                       ((vromStart == asset->originalVromEnd) && (vromEnd != vromStart))) {
                left = mid + 1;
            } else if (OotPort_RangeContains(asset->originalVromStart, asset->originalVromEnd, vromStart, vromEnd)) {
                return asset;
            } else {
                return NULL;
            }
        }

        return NULL;
    }

    for (i = 0; i < gOotPortExternalAssetCount; i++) {
        const OotPortExternalAsset* asset = &gOotPortExternalAssets[i];

        if (OotPort_RangeContains(asset->originalVromStart, asset->originalVromEnd, vromStart, vromEnd)) {
            return asset;
        }
    }

    return NULL;
}

static s32 OotPort_RangesOverlap(uintptr_t firstStart, uintptr_t firstEnd, uintptr_t secondStart,
                                uintptr_t secondEnd) {
    if ((firstEnd <= firstStart) || (secondEnd <= secondStart)) {
        return false;
    }

    return (firstStart < secondEnd) && (secondStart < firstEnd);
}

static s32 OotPort_RamRangeFromPtr(const void* ptr, size_t size, uintptr_t* ramStart, uintptr_t* ramEnd) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t normalizedStart;

    if ((ptr == NULL) || (size == 0) || (start > (UINTPTR_MAX - size))) {
        return false;
    }

    normalizedStart = start & 0x0FFFFFFFU;
    if ((normalizedStart >= OOT_PS2_NATIVE_ADDR_START) && (normalizedStart < OOT_PS2_NATIVE_ADDR_END)) {
        start = normalizedStart;
    }

    if (start > (UINTPTR_MAX - size)) {
        return false;
    }

    *ramStart = start;
    *ramEnd = start + size;
    return true;
}

static s32 OotPort_ClearLoadedAssetSerialRanges(uintptr_t ramStart, uintptr_t ramEnd) {
    size_t left = 0;
    size_t right = sOotPs2LoadedAssetSerialRangeCount;
    size_t first;
    size_t last;

    while (left < right) {
        size_t mid = left + ((right - left) / 2);

        if (sOotPs2LoadedAssetSerialRanges[mid].ramEnd <= ramStart) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    first = left;
    last = first;
    while ((last < sOotPs2LoadedAssetSerialRangeCount) &&
           (sOotPs2LoadedAssetSerialRanges[last].ramStart < ramEnd)) {
        last++;
    }

    if (first == last) {
        return false;
    }

    if (last < sOotPs2LoadedAssetSerialRangeCount) {
        memmove(&sOotPs2LoadedAssetSerialRanges[first], &sOotPs2LoadedAssetSerialRanges[last],
                (sOotPs2LoadedAssetSerialRangeCount - last) * sizeof(sOotPs2LoadedAssetSerialRanges[0]));
    }
    sOotPs2LoadedAssetSerialRangeCount -= last - first;
    return true;
}

static void OotPort_StoreLoadedAssetSerialRange(uintptr_t ramStart, uintptr_t ramEnd, u32 serial, u32 flags) {
    size_t left = 0;
    size_t right = sOotPs2LoadedAssetSerialRangeCount;
    size_t insertIndex;

    if (sOotPs2LoadedAssetSerialRangeCount >= OOT_PS2_LOADED_ASSET_RANGE_COUNT) {
        sOotPs2LoadedAssetSerialRangeIndexComplete = false;
        return;
    }

    while (left < right) {
        size_t mid = left + ((right - left) / 2);

        if (sOotPs2LoadedAssetSerialRanges[mid].ramStart < ramStart) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    insertIndex = left;
    if (insertIndex < sOotPs2LoadedAssetSerialRangeCount) {
        memmove(&sOotPs2LoadedAssetSerialRanges[insertIndex + 1],
                &sOotPs2LoadedAssetSerialRanges[insertIndex],
                (sOotPs2LoadedAssetSerialRangeCount - insertIndex) * sizeof(sOotPs2LoadedAssetSerialRanges[0]));
    }

    sOotPs2LoadedAssetSerialRanges[insertIndex].ramStart = ramStart;
    sOotPs2LoadedAssetSerialRanges[insertIndex].ramEnd = ramEnd;
    sOotPs2LoadedAssetSerialRanges[insertIndex].serial = serial;
    sOotPs2LoadedAssetSerialRanges[insertIndex].flags = flags;
    sOotPs2LoadedAssetSerialRangeCount++;
}

static inline __attribute__((always_inline)) const OotPs2LoadedAssetSerialRange*
OotPort_FindLoadedAssetSerialRange(uintptr_t ramStart) {
    size_t left = 0;
    size_t right = sOotPs2LoadedAssetSerialRangeCount;
    const OotPs2LoadedAssetSerialRange* range;

    while (left < right) {
        size_t mid = left + ((right - left) / 2);

        if (sOotPs2LoadedAssetSerialRanges[mid].ramStart <= ramStart) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    if (left == 0) {
        return NULL;
    }

    range = &sOotPs2LoadedAssetSerialRanges[left - 1];
    return (ramStart < range->ramEnd) ? range : NULL;
}

s32 OotPs2Asset_IsMutableTextureRange(uintptr_t start, uintptr_t end) {
    if (end <= start) return false;
    for (int i = 0; i < 2; i++) {
        if (sOotPs2DodongoFloorStart[i] != 0 && start >= sOotPs2DodongoFloorStart[i] &&
            end <= sOotPs2DodongoFloorEnd[i]) return true;
    }
    return false;
}

static void OotPort_ClearLoadedAssetRange(void* ram, size_t size) {
    uintptr_t ramStart;
    uintptr_t ramEnd;
    size_t i;
    s32 cleared;

    if (!OotPort_RamRangeFromPtr(ram, size, &ramStart, &ramEnd)) {
        return;
    }

    cleared = OotPort_ClearLoadedAssetSerialRanges(ramStart, ramEnd);
    for (i = 0; i < 2; i++) {
        if (sOotPs2DodongoFloorStart[i] != 0 &&
            OotPort_RangesOverlap(sOotPs2DodongoFloorStart[i], sOotPs2DodongoFloorEnd[i], ramStart, ramEnd)) {
            sOotPs2DodongoFloorStart[i] = 0;
            sOotPs2DodongoFloorEnd[i] = 0;
        }
    }

    for (i = 0; i < sOotPs2LoadedAssetRangeHighWater; i++) {
        OotPs2LoadedAssetRange* range = &sOotPs2LoadedAssetRanges[i];

        if (OotPort_RangesOverlap(range->ramStart, range->ramEnd, ramStart, ramEnd)) {
            OotPort_ForgetNativeTextureRangeCache(range);
            range->ramStart = 0;
            range->ramEnd = 0;
            range->assetOffsetStart = 0;
            range->flags = 0;
            range->serial = 0;
            cleared = true;
        }
    }

    if (cleared) {
        OotPort_ClearAssetRangeSerialCache();
        OotPort_ClearNativeByteRangeCache();
    }
}

static void OotPort_StoreLoadedAssetRange(void* ram, size_t size, uintptr_t assetOffsetStart, u32 flags, u32 serial) {
    OotPs2LoadedAssetRange* range;
    uintptr_t ramStart;
    uintptr_t ramEnd;
    size_t i;
    size_t slot = OOT_PS2_LOADED_ASSET_RANGE_COUNT;

    if (!OotPort_RamRangeFromPtr(ram, size, &ramStart, &ramEnd)) {
        return;
    }

    for (i = 0; i < OOT_PS2_LOADED_ASSET_RANGE_COUNT; i++) {
        size_t candidate = sOotPs2LoadedAssetRangeNext + i;

        if (candidate >= OOT_PS2_LOADED_ASSET_RANGE_COUNT) {
            candidate -= OOT_PS2_LOADED_ASSET_RANGE_COUNT;
        }
        if (sOotPs2LoadedAssetRanges[candidate].ramStart == sOotPs2LoadedAssetRanges[candidate].ramEnd) {
            slot = candidate;
            break;
        }
    }

    if (slot == OOT_PS2_LOADED_ASSET_RANGE_COUNT) {
        slot = sOotPs2LoadedAssetRangeNext;
    }
    sOotPs2LoadedAssetRangeNext = (slot + 1) % OOT_PS2_LOADED_ASSET_RANGE_COUNT;
    if (slot >= sOotPs2LoadedAssetRangeHighWater) {
        sOotPs2LoadedAssetRangeHighWater = slot + 1;
    }

    range = &sOotPs2LoadedAssetRanges[slot];
    if (range->serial != 0) {
        OotPort_ForgetNativeTextureRangeCache(range);
        OotPort_ClearNativeByteRangeCache();
    }
    range->ramStart = ramStart;
    range->ramEnd = ramEnd;
    range->assetOffsetStart = assetOffsetStart;
    range->flags = flags;
    range->serial = serial;
}

static void OotPort_ForgetNativeTextureRangeCache(const OotPs2LoadedAssetRange* range) {
    size_t i;

    if (sOotPs2LastNativeTextureRange == range) {
        sOotPs2LastNativeTextureRange = NULL;
    }
    if (sOotPs2LastNativeAssetRange == range) {
        sOotPs2LastNativeAssetRange = NULL;
    }

    for (i = 0; i < OOT_PS2_NATIVE_TEXTURE_RANGE_CACHE_COUNT; i++) {
        if (sOotPs2NativeTextureRangeCache[i] == range) {
            sOotPs2NativeTextureRangeCache[i] = NULL;
        }
    }

    for (i = 0; i < OOT_PS2_NATIVE_ASSET_RANGE_CACHE_COUNT; i++) {
        if (sOotPs2NativeAssetRangeCache[i] == range) {
            sOotPs2NativeAssetRangeCache[i] = NULL;
        }
    }
}

static inline __attribute__((always_inline)) s32
OotPort_IsNativeLoadedAssetByteRange(const OotPs2LoadedAssetRange* range, uintptr_t ram) {
    return (range != NULL) && (range->serial != 0) && ((range->flags & OOT_PORT_EXTERNAL_ASSET_NATIVE) != 0) &&
           (ram >= range->ramStart) && (ram < range->ramEnd);
}

static inline __attribute__((always_inline)) s32
OotPort_IsNativeLoadedTextureByteRange(const OotPs2LoadedAssetRange* range, uintptr_t ram) {
    return OotPort_IsNativeLoadedAssetByteRange(range, ram) &&
           ((range->flags & OOT_PORT_EXTERNAL_ASSET_TEXTURE_WORDS) != 0);
}

static void OotPort_RememberNativeTextureRange(const OotPs2LoadedAssetRange* range) {
    size_t i;

    if ((range == NULL) || ((range->flags & OOT_PORT_EXTERNAL_ASSET_TEXTURE_WORDS) == 0)) {
        return;
    }

    for (i = 0; i < OOT_PS2_NATIVE_TEXTURE_RANGE_CACHE_COUNT; i++) {
        if (sOotPs2NativeTextureRangeCache[i] == range) {
            return;
        }
    }

    sOotPs2NativeTextureRangeCache[sOotPs2NativeTextureRangeCacheNext] = range;
    sOotPs2NativeTextureRangeCacheNext =
        (sOotPs2NativeTextureRangeCacheNext + 1) % OOT_PS2_NATIVE_TEXTURE_RANGE_CACHE_COUNT;
}

static inline __attribute__((always_inline)) const OotPs2LoadedAssetRange*
OotPort_FindCachedNativeTextureRange(uintptr_t ram) {
    size_t i;

    if (OotPort_IsNativeLoadedTextureByteRange(sOotPs2LastNativeTextureRange, ram)) {
        return sOotPs2LastNativeTextureRange;
    }

    for (i = 0; i < OOT_PS2_NATIVE_TEXTURE_RANGE_CACHE_COUNT; i++) {
        const OotPs2LoadedAssetRange* range = sOotPs2NativeTextureRangeCache[i];

        if (OotPort_IsNativeLoadedTextureByteRange(range, ram)) {
            sOotPs2LastNativeTextureRange = range;
            return range;
        }
    }

    return NULL;
}

static void OotPort_ClearNativeByteRangeCache(void) {
    sOotPs2NativeByteRangeCacheGeneration++;
    if (sOotPs2NativeByteRangeCacheGeneration == 0) {
        memset(sOotPs2NativeByteRangeCache, 0, sizeof(sOotPs2NativeByteRangeCache));
        sOotPs2NativeByteRangeCacheGeneration = 1;
    }
}

static inline __attribute__((always_inline)) size_t OotPort_NativeByteRangeCacheIndex(uintptr_t ram) {
    return (size_t)((ram >> OOT_PS2_NATIVE_BYTE_RANGE_PAGE_SHIFT) &
                    (OOT_PS2_NATIVE_BYTE_RANGE_CACHE_COUNT - 1));
}

static inline __attribute__((always_inline)) const OotPs2LoadedAssetRange*
OotPort_FindCachedNativeByteRange(uintptr_t ram) {
    uintptr_t page = ram >> OOT_PS2_NATIVE_BYTE_RANGE_PAGE_SHIFT;
    const OotPs2NativeByteRangeCacheEntry* entry =
        &sOotPs2NativeByteRangeCache[OotPort_NativeByteRangeCacheIndex(ram)];
    const OotPs2LoadedAssetRange* range = entry->range;

    if ((entry->generation == sOotPs2NativeByteRangeCacheGeneration) && (entry->page == page) &&
        OotPort_IsNativeLoadedAssetByteRange(range, ram)) {
        return range;
    }

    return NULL;
}

static inline __attribute__((always_inline)) void
OotPort_RememberNativeByteRange(const OotPs2LoadedAssetRange* range, uintptr_t ram) {
    OotPs2NativeByteRangeCacheEntry* entry;

    if (!OotPort_IsNativeLoadedAssetByteRange(range, ram)) {
        return;
    }

    entry = &sOotPs2NativeByteRangeCache[OotPort_NativeByteRangeCacheIndex(ram)];
    entry->page = ram >> OOT_PS2_NATIVE_BYTE_RANGE_PAGE_SHIFT;
    entry->range = range;
    entry->generation = sOotPs2NativeByteRangeCacheGeneration;
}

static void OotPort_RememberNativeAssetRange(const OotPs2LoadedAssetRange* range) {
    size_t i;

    if ((range == NULL) || ((range->flags & OOT_PORT_EXTERNAL_ASSET_NATIVE) == 0)) {
        return;
    }

    for (i = 0; i < OOT_PS2_NATIVE_ASSET_RANGE_CACHE_COUNT; i++) {
        if (sOotPs2NativeAssetRangeCache[i] == range) {
            return;
        }
    }

    sOotPs2NativeAssetRangeCache[sOotPs2NativeAssetRangeCacheNext] = range;
    sOotPs2NativeAssetRangeCacheNext =
        (sOotPs2NativeAssetRangeCacheNext + 1) % OOT_PS2_NATIVE_ASSET_RANGE_CACHE_COUNT;
}

static inline __attribute__((always_inline)) const OotPs2LoadedAssetRange*
OotPort_FindCachedNativeAssetRange(uintptr_t ramStart, uintptr_t ramEnd) {
    size_t i;

    if (OotPort_IsNativeLoadedAssetByteRange(sOotPs2LastNativeAssetRange, ramStart) &&
        OotPort_RangeContains(sOotPs2LastNativeAssetRange->ramStart, sOotPs2LastNativeAssetRange->ramEnd, ramStart, ramEnd)) {
        return sOotPs2LastNativeAssetRange;
    }

    for (i = 0; i < OOT_PS2_NATIVE_ASSET_RANGE_CACHE_COUNT; i++) {
        const OotPs2LoadedAssetRange* range = sOotPs2NativeAssetRangeCache[i];

        if (OotPort_IsNativeLoadedAssetByteRange(range, ramStart) &&
            OotPort_RangeContains(range->ramStart, range->ramEnd, ramStart, ramEnd)) {
            sOotPs2LastNativeAssetRange = range;
            return range;
        }
    }

    return NULL;
}

static s32 OotPort_MapNativeExternalTextureByteInRange(const OotPs2LoadedAssetRange* range, uintptr_t ram,
                                                      const void** mapped) {
    uintptr_t assetOffset;
    uintptr_t assetSpan;
    uintptr_t relativeAssetOffset;
    uintptr_t mappedRelativeAssetOffset;
    uintptr_t mappedRam;

    assetSpan = range->ramEnd - range->ramStart;
    if (range->assetOffsetStart > (UINTPTR_MAX - assetSpan)) {
        return false;
    }

    assetOffset = range->assetOffsetStart + (ram - range->ramStart);
    relativeAssetOffset = assetOffset - range->assetOffsetStart;
    mappedRelativeAssetOffset = relativeAssetOffset ^ 7U;

    if (mappedRelativeAssetOffset >= assetSpan) {
        return false;
    }

    mappedRam = range->ramStart + mappedRelativeAssetOffset;
    *mapped = (const void*)mappedRam;
    return true;
}

static void OotPort_RegisterLoadedAssetRanges(void* ram, size_t size, uintptr_t vromStart,
                                             const OotPortExternalAsset* asset, u32 serial) {
    uintptr_t ramStart;
    uintptr_t ramEnd;
    uintptr_t vromEnd;
    size_t i;

    if ((asset == NULL) || !OotPort_RamRangeFromPtr(ram, size, &ramStart, &ramEnd)) {
        return;
    }

    if (asset->name != NULL && strcmp(asset->name, "ddan_boss_room_1") == 0 && vromStart <= UINTPTR_MAX - size) {
        const uintptr_t loadEnd = vromStart + size;
        const uintptr_t offsets[2] = { 0x21C8U, 0x21D8U };
        const size_t floorBytes = 32U * 64U * 2U;
        for (int m = 0; m < 2; m++) {
            const uintptr_t floorVrom = asset->vromStart + offsets[m];
            if (floorVrom >= vromStart && floorVrom <= UINTPTR_MAX - floorBytes &&
                floorVrom + floorBytes <= loadEnd) {
                sOotPs2DodongoFloorStart[m] = ramStart + (floorVrom - vromStart);
                sOotPs2DodongoFloorEnd[m] = sOotPs2DodongoFloorStart[m] + floorBytes;
            }
        }
    }

    OotPort_StoreLoadedAssetRange((void*)ramStart, size, vromStart, asset->flags & ~OOT_PORT_EXTERNAL_ASSET_TEXTURE_WORDS,
                                 serial);
    OotPort_StoreLoadedAssetSerialRange(ramStart, ramEnd, serial,
                                       asset->flags & ~OOT_PORT_EXTERNAL_ASSET_TEXTURE_WORDS);
    OotPort_ClearAssetRangeSerialCache();

    if ((asset->flags & (OOT_PORT_EXTERNAL_ASSET_NATIVE | OOT_PORT_EXTERNAL_ASSET_TEXTURE_WORDS)) !=
        (OOT_PORT_EXTERNAL_ASSET_NATIVE | OOT_PORT_EXTERNAL_ASSET_TEXTURE_WORDS)) {
        return;
    }

    if (vromStart > (UINTPTR_MAX - size)) {
        return;
    }

    vromEnd = vromStart + size;

    for (i = 0; i < gOotPortExternalAssetTextureRangeCount; i++) {
        const OotPortExternalAssetTextureRange* textureRange = &gOotPortExternalAssetTextureRanges[i];
        uintptr_t overlapStart;
        uintptr_t overlapEnd;

        if (textureRange->vromEnd <= vromStart) {
            continue;
        }

        if (textureRange->vromStart >= vromEnd) {
            break;
        }

        overlapStart = textureRange->vromStart > vromStart ? textureRange->vromStart : vromStart;
        overlapEnd = textureRange->vromEnd < vromEnd ? textureRange->vromEnd : vromEnd;

        if (overlapEnd <= overlapStart) {
            continue;
        }

        OotPort_StoreLoadedAssetRange((void*)(ramStart + (overlapStart - vromStart)),
                                     (size_t)(overlapEnd - overlapStart), overlapStart, asset->flags, serial);
    }
}

static u32 OotPort_NextLoadedAssetSerial(void) {
    u32 serial = sOotPs2LoadedAssetSerial++;

    if (sOotPs2LoadedAssetSerial == 0) {
        sOotPs2LoadedAssetSerial = 1;
    }

    return serial;
}

static s32 OotPort_TryReadAssetCache(const OotPortExternalAsset* asset, void* ram, uintptr_t vrom, size_t size,
                                    s32 useFastCopy, s32 allowAudioYield) {
    OotPs2AssetWindowCache* cache;
    size_t offset;
    size_t cacheOffset;

    if ((asset == NULL) || (vrom < asset->vromStart)) {
        return false;
    }

    cache = OotPort_FindAssetCache(asset);
    offset = vrom - asset->vromStart;
    if ((cache == NULL) && (size <= OOT_PS2_HOT_READ_MAX_SIZE) && OotPort_RecordHotAssetRead(asset)) {
        cache = OotPort_GetHotAssetCacheCandidate(asset);
    }

    if (!OotPort_EnsureAssetCacheRange(cache, asset, offset, size, allowAudioYield)) {
        return false;
    }

    cacheOffset = offset - cache->offset;
    if ((cacheOffset > cache->dataSize) || (size > (cache->dataSize - cacheOffset))) {
        return false;
    }
    OotPort_CopyAssetBytes(ram, &cache->data[cacheOffset], size, useFastCopy);
    OotPort_RegisterLoadedAssetRanges(ram, size, vrom, asset, OotPort_NextLoadedAssetSerial());
    return true;
}

static s32 OotPort_TryTranslateAssetRange(const OotPortExternalAsset* asset, uintptr_t rangeStart, uintptr_t rangeEnd,
                                         uintptr_t vromStart, uintptr_t vromEnd, uintptr_t* normalizedStart,
                                         uintptr_t* normalizedEnd) {
    if (!OotPort_RangeContains(rangeStart, rangeEnd, vromStart, vromEnd)) {
        return false;
    }

    *normalizedStart = asset->vromStart + (vromStart - rangeStart);
    *normalizedEnd = asset->vromStart + (vromEnd - rangeStart);
    return true;
}

static s32 OotPort_TryNormalizeExternalRange(uintptr_t vromStart, uintptr_t vromEnd, uintptr_t* normalizedStart,
                                            uintptr_t* normalizedEnd) {
    size_t assetIndex;
    const OotPortExternalAsset* asset;

    if (OotPort_FindContainingExternalAsset(vromStart, &assetIndex) != NULL) {
        if (OotPort_IsExternalAssetSpanContiguous(assetIndex, vromStart, vromEnd)) {
            *normalizedStart = vromStart;
            *normalizedEnd = vromEnd;
            return true;
        }
    }

    asset = OotPort_FindContainingOriginalExternalAssetRange(vromStart, vromEnd);
    if ((asset != NULL) &&
        OotPort_TryTranslateAssetRange(asset, asset->originalVromStart, asset->originalVromEnd, vromStart, vromEnd,
                                      normalizedStart, normalizedEnd)) {
        return true;
    }

    return false;
}

static s32 OotPort_NormalizeVromRange(uintptr_t vromStart, uintptr_t vromEnd, uintptr_t* normalizedStart,
                                     uintptr_t* normalizedEnd) {
    if ((normalizedStart == NULL) || (normalizedEnd == NULL) || (vromEnd < vromStart)) {
        return false;
    }

    return OotPort_TryNormalizeExternalRange(vromStart, vromEnd, normalizedStart, normalizedEnd);
}

uintptr_t OotPort_NormalizeVrom(uintptr_t vrom) {
    uintptr_t normalizedStart;
    uintptr_t normalizedEnd;

    if (OotPort_NormalizeVromRange(vrom, vrom, &normalizedStart, &normalizedEnd)) {
        return normalizedStart;
    }

    return vrom;
}

void OotPort_NormalizeRomFile(RomFile* file) {
    uintptr_t normalizedStart;
    uintptr_t normalizedEnd;

    if ((file == NULL) || (file->vromStart == 0)) {
        return;
    }

    if (OotPort_NormalizeVromRange(file->vromStart, file->vromEnd, &normalizedStart, &normalizedEnd)) {
        file->vromStart = normalizedStart;
        file->vromEnd = normalizedEnd;
    }
}

const OotPortMessageEntry* OotPort_FindMessageEntry(const OotPortMessageEntry* entries, size_t count, u16 textId) {
    size_t left = 0;
    size_t right = count;

    while (left < right) {
        size_t mid = left + ((right - left) / 2);

        if (entries[mid].textId < textId) {
            left = mid + 1;
        } else if (entries[mid].textId > textId) {
            right = mid;
        } else {
            return &entries[mid];
        }
    }

    return NULL;
}

s32 OotPort_GetLoadedExternalAssetRangeFlags(const void* ptr, size_t size, u32* flags) {
    const OotPs2LoadedAssetSerialRange* indexedRange;
    uintptr_t ramStart;
    uintptr_t ramEnd;
    size_t i;

    if (flags != NULL) {
        *flags = 0;
    }

    if (!OotPort_RamRangeFromPtr(ptr, size, &ramStart, &ramEnd)) {
        return false;
    }

    indexedRange = OotPort_FindLoadedAssetSerialRange(ramStart);
    if ((indexedRange != NULL) &&
        OotPort_RangeContains(indexedRange->ramStart, indexedRange->ramEnd, ramStart, ramEnd)) {
        if (flags != NULL) {
            *flags = indexedRange->flags;
        }
        return true;
    }

    if (sOotPs2LoadedAssetSerialRangeIndexComplete) {
        return false;
    }

    for (i = 0; i < sOotPs2LoadedAssetRangeHighWater; i++) {
        const OotPs2LoadedAssetRange* range = &sOotPs2LoadedAssetRanges[i];

        if ((range->serial != 0) && OotPort_RangeContains(range->ramStart, range->ramEnd, ramStart, ramEnd)) {
            if (flags != NULL) {
                *flags = range->flags;
            }
            if ((range->flags & OOT_PORT_EXTERNAL_ASSET_NATIVE) != 0) {
                OotPort_RememberNativeTextureRange(range);
            }
            return true;
        }
    }

    return false;
}

s32 OotPort_MarkLoadedExternalAssetRangeFlags(const void* ptr, size_t size, u32 flags) {
    uintptr_t ramStart;
    uintptr_t ramEnd;
    size_t i;
    s32 marked = false;

    if ((flags == 0) || !OotPort_RamRangeFromPtr(ptr, size, &ramStart, &ramEnd)) {
        return false;
    }

    for (i = 0; i < sOotPs2LoadedAssetSerialRangeCount; i++) {
        OotPs2LoadedAssetSerialRange* range = &sOotPs2LoadedAssetSerialRanges[i];
        if (OotPort_RangeContains(range->ramStart, range->ramEnd, ramStart, ramEnd)) {
            range->flags |= flags;
            marked = true;
        }
    }

    for (i = 0; i < sOotPs2LoadedAssetRangeHighWater; i++) {
        OotPs2LoadedAssetRange* range = &sOotPs2LoadedAssetRanges[i];
        if ((range->serial != 0) && OotPort_RangeContains(range->ramStart, range->ramEnd, ramStart, ramEnd)) {
            OotPort_ForgetNativeTextureRangeCache(range);
            range->flags |= flags;
            marked = true;
        }
    }

    if (marked) {
        OotPort_ClearAssetRangeSerialCache();
        OotPort_ClearNativeByteRangeCache();
    }
    return marked;
}

s32 OotPort_IsLoadedNativeExternalAssetRange(const void* ptr, size_t size) {
    u32 flags;

    if (!OotPort_GetLoadedExternalAssetRangeFlags(ptr, size, &flags)) {
        return false;
    }

    return (flags & OOT_PORT_EXTERNAL_ASSET_NATIVE) != 0;
}

s32 OotPort_IsNativeExternalTextureRange(const void* ptr, size_t size) {
    uintptr_t ramStart;
    uintptr_t ramEnd;
    size_t i;

    if (!OotPort_RamRangeFromPtr(ptr, size, &ramStart, &ramEnd)) {
        return false;
    }

    for (i = 0; i < sOotPs2LoadedAssetRangeHighWater; i++) {
        const OotPs2LoadedAssetRange* range = &sOotPs2LoadedAssetRanges[i];

        if (((range->flags & (OOT_PORT_EXTERNAL_ASSET_NATIVE | OOT_PORT_EXTERNAL_ASSET_TEXTURE_WORDS)) ==
             (OOT_PORT_EXTERNAL_ASSET_NATIVE | OOT_PORT_EXTERNAL_ASSET_TEXTURE_WORDS)) &&
            OotPort_RangeContains(range->ramStart, range->ramEnd, ramStart, ramEnd)) {
            OotPort_RememberNativeTextureRange(range);
            return true;
        }
    }

    return false;
}

u32 OotPort_GetExternalAssetRangeSerial(const void* ptr, size_t size) {
    uintptr_t ramStart;
    uintptr_t ramEnd;
    uintptr_t cacheEnd;
    u32 serial;

    if (!OotPort_RamRangeFromPtr(ptr, size, &ramStart, &ramEnd)) {
        return 0;
    }
    if (OotPs2Asset_IsMutableTextureRange(ramStart, ramEnd)) {
        return 0;
    }

    if (OotPort_GetCachedAssetRangeSerial(ramStart, ramEnd, &serial)) {
        return serial;
    }

    cacheEnd = ramEnd;
    {
        const OotPs2LoadedAssetSerialRange* range = OotPort_FindLoadedAssetSerialRange(ramStart);

        if ((range != NULL) && OotPort_RangeContains(range->ramStart, range->ramEnd, ramStart, ramEnd)) {
            serial = range->serial;
            cacheEnd = range->ramEnd;
        } else {
            serial = 0;
        }
    }
    if ((serial == 0) && !sOotPs2LoadedAssetSerialRangeIndexComplete) {
        size_t i;

        for (i = 0; i < sOotPs2LoadedAssetRangeHighWater; i++) {
            const OotPs2LoadedAssetRange* range = &sOotPs2LoadedAssetRanges[i];

            if ((range->serial != 0) &&
                OotPort_RangeContains(range->ramStart, range->ramEnd, ramStart, ramEnd)) {
                serial = range->serial;
                cacheEnd = range->ramEnd;
                break;
            }
        }
    }

    OotPort_RememberAssetRangeSerial(ramStart, cacheEnd, serial);
    return serial;
}

s32 OotPort_GetNativeExternalTextureMappingRange(const void* ptr, uintptr_t* ramStart, uintptr_t* ramEnd) {
    uintptr_t ram;
    uintptr_t ignoredEnd;
    size_t i;

    if ((ramStart == NULL) || (ramEnd == NULL) || !OotPort_RamRangeFromPtr(ptr, 1, &ram, &ignoredEnd)) {
        return false;
    }

    {
        const OotPs2LoadedAssetRange* range = OotPort_FindCachedNativeTextureRange(ram);

        if (range != NULL) {
            *ramStart = range->ramStart;
            *ramEnd = range->ramEnd;
            return true;
        }
    }

    for (i = 0; i < sOotPs2LoadedAssetRangeHighWater; i++) {
        const OotPs2LoadedAssetRange* range = &sOotPs2LoadedAssetRanges[i];

        if (!OotPort_IsNativeLoadedTextureByteRange(range, ram)) {
            continue;
        }

        OotPort_RememberNativeTextureRange(range);
        OotPort_RememberNativeByteRange(range, ram);
        sOotPs2LastNativeTextureRange = range;
        *ramStart = range->ramStart;
        *ramEnd = range->ramEnd;
        return true;
    }

    {
        const OotPs2LoadedAssetRange* range = OotPort_FindCachedNativeAssetRange(ram, ram + 1);

        if (range != NULL) {
            *ramStart = range->ramStart;
            *ramEnd = range->ramEnd;
            return true;
        }
    }

    for (i = 0; i < sOotPs2LoadedAssetRangeHighWater; i++) {
        const OotPs2LoadedAssetRange* range = &sOotPs2LoadedAssetRanges[i];

        if (!OotPort_IsNativeLoadedAssetByteRange(range, ram)) {
            continue;
        }

        OotPort_RememberNativeAssetRange(range);
        OotPort_RememberNativeByteRange(range, ram);
        sOotPs2LastNativeAssetRange = range;
        *ramStart = range->ramStart;
        *ramEnd = range->ramEnd;
        return true;
    }

    return false;
}

s32 OotPort_GetNativeExternalTextureRangeStart(const void* ptr, size_t size, uintptr_t* rangeStart) {
    uintptr_t ramStart;
    uintptr_t ramEnd;
    size_t i;

    if ((rangeStart == NULL) || !OotPort_RamRangeFromPtr(ptr, size, &ramStart, &ramEnd)) {
        return false;
    }

    {
        const OotPs2LoadedAssetRange* range = OotPort_FindCachedNativeByteRange(ramStart);

        if ((range != NULL) && OotPort_RangeContains(range->ramStart, range->ramEnd, ramStart, ramEnd)) {
            *rangeStart = range->ramStart;
            return true;
        }
    }

    {
        const OotPs2LoadedAssetRange* range = OotPort_FindCachedNativeTextureRange(ramStart);

        if ((range != NULL) && OotPort_RangeContains(range->ramStart, range->ramEnd, ramStart, ramEnd)) {
            *rangeStart = range->ramStart;
            return true;
        }
    }

    for (i = 0; i < sOotPs2LoadedAssetRangeHighWater; i++) {
        const OotPs2LoadedAssetRange* range = &sOotPs2LoadedAssetRanges[i];

        if (((range->flags & (OOT_PORT_EXTERNAL_ASSET_NATIVE | OOT_PORT_EXTERNAL_ASSET_TEXTURE_WORDS)) ==
             (OOT_PORT_EXTERNAL_ASSET_NATIVE | OOT_PORT_EXTERNAL_ASSET_TEXTURE_WORDS)) &&
            (range->serial != 0) && OotPort_RangeContains(range->ramStart, range->ramEnd, ramStart, ramEnd)) {
            OotPort_RememberNativeTextureRange(range);
            OotPort_RememberNativeByteRange(range, ramStart);
            sOotPs2LastNativeTextureRange = range;
            *rangeStart = range->ramStart;
            return true;
        }
    }

    {
        const OotPs2LoadedAssetRange* range = OotPort_FindCachedNativeAssetRange(ramStart, ramEnd);

        if (range != NULL) {
            *rangeStart = range->ramStart;
            return true;
        }
    }

    for (i = 0; i < sOotPs2LoadedAssetRangeHighWater; i++) {
        const OotPs2LoadedAssetRange* range = &sOotPs2LoadedAssetRanges[i];

        if (OotPort_IsNativeLoadedAssetByteRange(range, ramStart) &&
            OotPort_RangeContains(range->ramStart, range->ramEnd, ramStart, ramEnd)) {
            OotPort_RememberNativeAssetRange(range);
            OotPort_RememberNativeByteRange(range, ramStart);
            sOotPs2LastNativeAssetRange = range;
            *rangeStart = range->ramStart;
            return true;
        }
    }

    return false;
}

s32 OotPort_MapNativeExternalTextureByte(const void* ptr, const void** mapped) {
    uintptr_t ram = (uintptr_t)ptr;
    uintptr_t normalizedRam;
    size_t i;

    if ((ptr == NULL) || (mapped == NULL)) {
        return false;
    }

    normalizedRam = ram & 0x0FFFFFFFU;
    if ((normalizedRam >= OOT_PS2_NATIVE_ADDR_START) && (normalizedRam < OOT_PS2_NATIVE_ADDR_END)) {
        ram = normalizedRam;
    }

    {
        const OotPs2LoadedAssetRange* cachedRange = OotPort_FindCachedNativeByteRange(ram);

        if (cachedRange != NULL) {
            return OotPort_MapNativeExternalTextureByteInRange(cachedRange, ram, mapped);
        }
    }

    {
        const OotPs2LoadedAssetRange* cachedRange = OotPort_FindCachedNativeTextureRange(ram);

        if (cachedRange != NULL) {
            OotPort_RememberNativeByteRange(cachedRange, ram);
            return OotPort_MapNativeExternalTextureByteInRange(cachedRange, ram, mapped);
        }
    }

    for (i = 0; i < sOotPs2LoadedAssetRangeHighWater; i++) {
        const OotPs2LoadedAssetRange* range = &sOotPs2LoadedAssetRanges[i];

        if (!OotPort_IsNativeLoadedTextureByteRange(range, ram)) {
            continue;
        }

        OotPort_RememberNativeTextureRange(range);
        OotPort_RememberNativeByteRange(range, ram);
        return OotPort_MapNativeExternalTextureByteInRange(range, ram, mapped);
    }

    {
        const OotPs2LoadedAssetRange* cachedRange = OotPort_FindCachedNativeAssetRange(ram, ram + 1);

        if (cachedRange != NULL) {
            return OotPort_MapNativeExternalTextureByteInRange(cachedRange, ram, mapped);
        }
    }

    for (i = 0; i < sOotPs2LoadedAssetRangeHighWater; i++) {
        const OotPs2LoadedAssetRange* range = &sOotPs2LoadedAssetRanges[i];

        if (!OotPort_IsNativeLoadedAssetByteRange(range, ram)) {
            continue;
        }

        OotPort_RememberNativeAssetRange(range);
        OotPort_RememberNativeByteRange(range, ram);
        sOotPs2LastNativeAssetRange = range;
        return OotPort_MapNativeExternalTextureByteInRange(range, ram, mapped);
    }

    return false;
}

static s32 OotPort_AssetReadLocked(void* ram, uintptr_t vrom, size_t size, s32 useFastCopy,
                                  s32 allowAudioYield) {
    uintptr_t normalizedVrom;
    uintptr_t normalizedEnd;
    uintptr_t cursor;
    size_t assetIndex;
    u8* out = ram;
    size_t remaining = size;

    if (size == 0) {
        return OOT_PORT_ASSET_READ_OK;
    }

    OotPort_ClearLoadedAssetRange(ram, size);

    if ((vrom > (UINTPTR_MAX - size)) ||
        !OotPort_NormalizeVromRange(vrom, vrom + size, &normalizedVrom, &normalizedEnd)) {
        return OOT_PORT_ASSET_READ_NOT_EXTERNAL;
    }

    cursor = normalizedVrom;
    (void)normalizedEnd;

    if (OotPort_FindContainingExternalAsset(normalizedVrom, &assetIndex) == NULL) {
        return OOT_PORT_ASSET_READ_NOT_EXTERNAL;
    }

    if (OotPort_TryReadAssetCache(&gOotPortExternalAssets[assetIndex], ram, normalizedVrom, size, useFastCopy,
                                 allowAudioYield)) {
        return OOT_PORT_ASSET_READ_OK;
    }

    while (remaining != 0) {
        const OotPortExternalAsset* asset = &gOotPortExternalAssets[assetIndex];
        uintptr_t offset;
        uintptr_t chunkVromStart;
        size_t chunkRemaining;
        size_t chunkSize;
        u8* chunkOut;
        u32 chunkSerial;

        if ((cursor < asset->vromStart) || (cursor >= asset->vromEnd)) {
            return OOT_PORT_ASSET_READ_NOT_EXTERNAL;
        }

        offset = cursor - asset->vromStart;
        chunkVromStart = cursor;
        chunkRemaining = asset->vromEnd - cursor;
        chunkSize = remaining < chunkRemaining ? remaining : chunkRemaining;
        chunkOut = out;
        chunkSerial = OotPort_NextLoadedAssetSerial();

        const s32 useBlockCache = chunkSize < OOT_PS2_PACKED_DIRECT_READ_MIN_SIZE;

        if (!OotPort_ReadPackedAssetFileRange(asset, offset, out, chunkSize, useBlockCache, useFastCopy,
                                             allowAudioYield)) {
            return OOT_PORT_ASSET_READ_FAILED;
        }

        out += chunkSize;
        cursor += chunkSize;
        remaining -= chunkSize;
        OotPort_RegisterLoadedAssetRanges(chunkOut, chunkSize, chunkVromStart, asset, chunkSerial);

        if (remaining != 0) {
            assetIndex++;
            if ((assetIndex >= gOotPortExternalAssetCount) ||
                (gOotPortExternalAssets[assetIndex].vromStart != cursor)) {
                printf("oot-ps2 asset span gap vrom=%08lx remaining=%lu\n", (unsigned long)cursor,
                       (unsigned long)remaining);
                return OOT_PORT_ASSET_READ_NOT_EXTERNAL;
            }
        }
    }

    return OOT_PORT_ASSET_READ_OK;
}

static void OotPort_WaitForForegroundAssetReads(void) {
    u32 waitStartUsec = OotPs2Time_GetUsecLow();

    while (OotPort_AssetReadHasForegroundPressure()) {
        u32 nowUsec = OotPs2Time_GetUsecLow();

        if ((s32)(nowUsec - waitStartUsec) >= OOT_PS2_AUDIO_READ_BACKOFF_MAX_USEC) {
            break;
        }
        OotPs2Thread_Delay(OOT_PS2_AUDIO_READ_BACKOFF_USEC);
    }
}

static s32 OotPort_AssetReadInternal(void* ram, uintptr_t vrom, size_t size, s32 isAudioRead, s32 urgentAudioRead) {
    s32 status;

    OotPort_InitAssetSema();
    if (isAudioRead) {
        if (!urgentAudioRead) {
            OotPort_WaitForForegroundAssetReads();
        }
    } else {
        sOotPs2ForegroundAssetReadWaiters++;
    }

    OotPort_LockAssetLoader();
    if (!isAudioRead) {
        sOotPs2ForegroundAssetReadWaiters--;
        sOotPs2ForegroundAssetReadActive++;
    }

    status = OotPort_AssetReadLocked(ram, vrom, size, !isAudioRead, !isAudioRead);
    if (isAudioRead && (status == OOT_PORT_ASSET_READ_OK)) {

        OotPort_WritebackCacheRange(ram, size);
    }
    if (!isAudioRead) {
        sOotPs2ForegroundAssetReadActive--;
    }
    OotPort_UnlockAssetLoader();
    return status;
}

s32 OotPort_AssetRead(void* ram, uintptr_t vrom, size_t size) {
    return OotPort_AssetReadInternal(ram, vrom, size, false, false);
}

s32 OotPort_AssetReadAudio(void* ram, uintptr_t vrom, size_t size) {
    return OotPort_AssetReadInternal(ram, vrom, size, true, false);
}

s32 OotPort_AssetReadAudioUrgent(void* ram, uintptr_t vrom, size_t size) {
    return OotPort_AssetReadInternal(ram, vrom, size, true, true);
}

s32 OotPort_AssetReadHasForegroundPressure(void) {
    return (sOotPs2ForegroundAssetReadWaiters > 0) || (sOotPs2ForegroundAssetReadActive > 0);
}
