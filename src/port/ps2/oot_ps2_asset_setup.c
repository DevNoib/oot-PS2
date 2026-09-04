#include "oot_ps2_asset_setup.h"

#include "oot_port_asset_loader.h"
#include "oot_ps2_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define OOT_PS2_INPUT_ROM_PATH "baserom.z64"
#define OOT_PS2_PACKED_ASSET_PATH "oot_ps2_assets.bin"
#define OOT_PS2_PACKED_ASSET_TEMP_PATH "oot_ps2_assets.tmp"
#define OOT_PS2_ROM_SIZE 0x02000000U
#define OOT_PS2_ROM_CRC32 0xCD16C529U
#define OOT_PS2_DMADATA_OFFSET 0x7430U
#define OOT_PS2_DMADATA_COUNT 1510U
#define OOT_PS2_TRANSFORM_ZERO_SELECTOR 56U
#define OOT_PS2_TRANSFORM_MAPPED_SELECTOR 57U
#define OOT_PS2_IO_CHUNK_SIZE 0x4000U
#define OOT_PS2_IO_ZERO_RETRY_COUNT 16
#define OOT_PS2_IO_ZERO_RETRY_USEC 1000

typedef struct OotPs2DmaEntry {
    u32 vromStart;
    u32 vromEnd;
    u32 romStart;
    u32 romEnd;
} OotPs2DmaEntry;

extern const u8 gOotPortAssetTransformCompressed[];
extern const u8 gOotPortAssetTransformCompressedEnd[];

static u32 sOotPs2AssetSetupProgress;
static s32 sOotPs2AssetSetupErrorShown;

static void OotPs2AssetSetup_ShowProgress(u32 progressPermille, const char* status) {
    sOotPs2AssetSetupProgress = progressPermille;
    printf("oot-ps2 asset setup %u/1000: %s\n", (unsigned)progressPermille, status);
}

static void OotPs2AssetSetup_ShowError(const char* status) {
    sOotPs2AssetSetupErrorShown = true;
    printf("oot-ps2 asset setup failed at %u/1000: %s\n", (unsigned)sOotPs2AssetSetupProgress, status);
}

static u32 OotPs2AssetSetup_ReadLe32(const u8* data) {
    return (u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16) | ((u32)data[3] << 24);
}

static u32 OotPs2AssetSetup_ReadBe32(const u8* data) {
    return ((u32)data[0] << 24) | ((u32)data[1] << 16) | ((u32)data[2] << 8) | (u32)data[3];
}

static s32 OotPs2AssetSetup_ReadAt(OotPs2Handle fd, u32 offset, void* output, size_t size) {
    u8* cursor = output;
    s32 zeroReads = 0;

    if (OotPs2File_Seek(fd, (OotPs2Offset)offset, OOT_PS2_SEEK_SET) < 0) {
        return false;
    }

    while (size != 0) {
        size_t chunk = size > OOT_PS2_IO_CHUNK_SIZE ? OOT_PS2_IO_CHUNK_SIZE : size;
        s32 read = OotPs2File_Read(fd, cursor, (u32)chunk);

        if (read < 0) {
            return false;
        }
        if (read == 0) {
            if (zeroReads++ >= OOT_PS2_IO_ZERO_RETRY_COUNT) {
                return false;
            }
            OotPs2Thread_Delay(OOT_PS2_IO_ZERO_RETRY_USEC);
            continue;
        }
        zeroReads = 0;
        cursor += read;
        size -= (size_t)read;
    }

    return true;
}

static s32 OotPs2AssetSetup_WriteAt(OotPs2Handle fd, u32 offset, const void* input, size_t size) {
    const u8* cursor = input;
    s32 zeroWrites = 0;

    if (OotPs2File_Seek(fd, (OotPs2Offset)offset, OOT_PS2_SEEK_SET) < 0) {
        return false;
    }

    while (size != 0) {
        size_t chunk = size > OOT_PS2_IO_CHUNK_SIZE ? OOT_PS2_IO_CHUNK_SIZE : size;
        s32 written = OotPs2File_Write(fd, cursor, (u32)chunk);

        if (written < 0) {
            return false;
        }
        if (written == 0) {
            if (zeroWrites++ >= OOT_PS2_IO_ZERO_RETRY_COUNT) {
                return false;
            }
            OotPs2Thread_Delay(OOT_PS2_IO_ZERO_RETRY_USEC);
            continue;
        }
        zeroWrites = 0;
        cursor += written;
        size -= (size_t)written;
    }

    return true;
}

static s32 OotPs2AssetSetup_CopyUncompressedAsset(OotPs2Handle romFd, OotPs2Handle outputFd, u32 romOffset,
                                                   u32 outputOffset, size_t size) {
    u8 buffer[OOT_PS2_IO_CHUNK_SIZE];

    while (size != 0) {
        size_t chunk = size > sizeof(buffer) ? sizeof(buffer) : size;

        if (!OotPs2AssetSetup_ReadAt(romFd, romOffset, buffer, chunk) ||
            !OotPs2AssetSetup_WriteAt(outputFd, outputOffset, buffer, chunk)) {
            return false;
        }
        romOffset += (u32)chunk;
        outputOffset += (u32)chunk;
        size -= chunk;
    }

    return true;
}

static s32 OotPs2AssetSetup_FileHasSize(const char* path, size_t expectedSize) {
    OotPs2Handle fd = OotPs2File_Open(path, OOT_PS2_FILE_RDONLY, 0);
    OotPs2Offset size;

    if (fd < 0) {
        return false;
    }

    size = OotPs2File_Seek(fd, 0, OOT_PS2_SEEK_END);
    OotPs2File_Close(fd);
    return (size >= 0) && ((size_t)size == expectedSize);
}

static OotPs2Handle OotPs2AssetSetup_OpenRom(char* pathBuffer, size_t pathBufferSize) {
    const char* path = OotPort_ResolveRootPath(OOT_PS2_INPUT_ROM_PATH, pathBuffer, pathBufferSize);

    return OotPs2File_Open(path, OOT_PS2_FILE_RDONLY, 0);
}

static s32 OotPs2AssetSetup_ValidateRom(OotPs2Handle fd) {
    static const u8 expectedHeader[16] = {
        0x80, 0x37, 0x12, 0x40, 0x00, 0x00, 0x00, 0x0F,
        0x80, 0x00, 0x04, 0x00, 0x00, 0x00, 0x14, 0x49,
    };
    u8* buffer;
    u8 header[16];
    uLong crc = crc32(0L, Z_NULL, 0);
    size_t remaining = OOT_PS2_ROM_SIZE;
    size_t processed = 0;
    OotPs2Offset size = OotPs2File_Seek(fd, 0, OOT_PS2_SEEK_END);

    if ((size < 0) || ((u32)size != OOT_PS2_ROM_SIZE) ||
        !OotPs2AssetSetup_ReadAt(fd, 0, header, sizeof(header)) ||
        (memcmp(header, expectedHeader, sizeof(header)) != 0)) {
        OotPs2AssetSetup_ShowError("NTSC 1.0 z64 ROM required");
        return false;
    }

    buffer = malloc(OOT_PS2_IO_CHUNK_SIZE);
    if (buffer == NULL) {
        OotPs2AssetSetup_ShowError("Not enough memory to validate ROM");
        return false;
    }

    if (OotPs2File_Seek(fd, 0, OOT_PS2_SEEK_SET) < 0) {
        free(buffer);
        OotPs2AssetSetup_ShowError("Could not read the ROM");
        return false;
    }

    while (remaining != 0) {
        size_t chunk = remaining > OOT_PS2_IO_CHUNK_SIZE ? OOT_PS2_IO_CHUNK_SIZE : remaining;
        s32 read = OotPs2File_Read(fd, buffer, (u32)chunk);

        if (read <= 0) {
            free(buffer);
            OotPs2AssetSetup_ShowError("Could not read the ROM");
            return false;
        }

        crc = crc32(crc, buffer, read);
        remaining -= (size_t)read;
        processed += (size_t)read;

        if (((processed & 0xFFFFF) == 0) || (remaining == 0)) {
            char status[64];

            snprintf(status, sizeof(status), "Validating ROM: %lu / 32 MB", (unsigned long)(processed >> 20));
            OotPs2AssetSetup_ShowProgress((u32)((processed * 100) / OOT_PS2_ROM_SIZE), status);
        }
    }

    free(buffer);

    if ((u32)crc != OOT_PS2_ROM_CRC32) {
        OotPs2AssetSetup_ShowError("ROM checksum failed - NTSC 1.0 required");
        return false;
    }

    return true;
}

static s32 OotPs2AssetSetup_LoadDmaTable(OotPs2Handle fd, OotPs2DmaEntry* entries) {
    u8 raw[OOT_PS2_DMADATA_COUNT * 16];
    size_t i;

    if (!OotPs2AssetSetup_ReadAt(fd, OOT_PS2_DMADATA_OFFSET, raw, sizeof(raw))) {
        return false;
    }

    for (i = 0; i < OOT_PS2_DMADATA_COUNT; i++) {
        const u8* input = &raw[i * 16];

        entries[i].vromStart = OotPs2AssetSetup_ReadBe32(input + 0);
        entries[i].vromEnd = OotPs2AssetSetup_ReadBe32(input + 4);
        entries[i].romStart = OotPs2AssetSetup_ReadBe32(input + 8);
        entries[i].romEnd = OotPs2AssetSetup_ReadBe32(input + 12);
    }

    return true;
}

static const OotPs2DmaEntry* OotPs2AssetSetup_FindDmaEntry(const OotPs2DmaEntry* entries,
                                                           const OotPortExternalAsset* asset) {
    size_t i;

    for (i = 0; i < OOT_PS2_DMADATA_COUNT; i++) {
        if ((entries[i].vromStart == asset->originalVromStart) && (entries[i].vromEnd == asset->originalVromEnd)) {
            return &entries[i];
        }
    }

    for (i = 0; i < OOT_PS2_DMADATA_COUNT; i++) {
        if ((entries[i].vromStart <= asset->originalVromStart) && (entries[i].vromEnd >= asset->originalVromEnd)) {
            return &entries[i];
        }
    }

    return NULL;
}

static s32 OotPs2AssetSetup_DecompressYaz0(const u8* input, size_t inputSize, u8* output, size_t outputSize) {
    const u8* inputEnd = input + inputSize;
    u8* outputStart = output;
    u8* outputEnd = output + outputSize;
    u8 code = 0;
    u32 validBits = 0;

    if ((inputSize < 16) || (memcmp(input, "Yaz0", 4) != 0) ||
        (OotPs2AssetSetup_ReadBe32(input + 4) != outputSize)) {
        return false;
    }

    input += 16;

    while (output < outputEnd) {
        if (validBits == 0) {
            if (input >= inputEnd) {
                return false;
            }
            code = *input++;
            validBits = 8;
        }

        if ((code & 0x80) != 0) {
            if (input >= inputEnd) {
                return false;
            }
            *output++ = *input++;
        } else {
            size_t distance;
            size_t length;
            u8* copy;

            if ((size_t)(inputEnd - input) < 2) {
                return false;
            }

            distance = (((size_t)input[0] & 0x0F) << 8) | input[1];
            length = input[0] >> 4;
            input += 2;

            if (length == 0) {
                if (input >= inputEnd) {
                    return false;
                }
                length = (size_t)*input++ + 0x12;
            } else {
                length += 2;
            }

            if ((distance + 1 > (size_t)(output - outputStart)) || (length > (size_t)(outputEnd - output))) {
                return false;
            }

            copy = output - distance - 1;
            while (length-- != 0) {
                *output++ = *copy++;
            }
        }

        code <<= 1;
        validBits--;
    }

    return true;
}

static s32 OotPs2AssetSetup_LoadAsset(OotPs2Handle romFd, const OotPs2DmaEntry* dma,
                                      const OotPortExternalAsset* asset, u8** output, size_t outputSize) {
    size_t entrySize = dma->vromEnd - dma->vromStart;
    size_t storedSize = dma->romEnd != 0 ? dma->romEnd - dma->romStart : entrySize;
    size_t assetOffset = asset->originalVromStart - dma->vromStart;
    u8* stored;
    u8* decoded;
    u8* result;

    if ((dma->romStart == 0xFFFFFFFFU) || (storedSize == 0) || (assetOffset + outputSize > entrySize)) {
        return false;
    }

    stored = malloc(storedSize);
    if ((stored == NULL) || !OotPs2AssetSetup_ReadAt(romFd, dma->romStart, stored, storedSize)) {
        free(stored);
        return false;
    }

    if (dma->romEnd == 0) {
        if ((assetOffset == 0) && (outputSize == entrySize)) {
            *output = stored;
            return true;
        }

        result = malloc(outputSize);
        if (result == NULL) {
            free(stored);
            return false;
        }

        memcpy(result, stored + assetOffset, outputSize);
        free(stored);
        *output = result;
        return true;
    }

    decoded = malloc(entrySize);
    if ((decoded == NULL) || !OotPs2AssetSetup_DecompressYaz0(stored, storedSize, decoded, entrySize)) {
        free(decoded);
        free(stored);
        return false;
    }

    free(stored);

    if ((assetOffset == 0) && (outputSize == entrySize)) {
        *output = decoded;
        return true;
    }

    result = malloc(outputSize);
    if (result == NULL) {
        free(decoded);
        return false;
    }

    memcpy(result, decoded + assetOffset, outputSize);
    free(decoded);
    *output = result;
    return true;
}

static s32 OotPs2AssetSetup_TransformAsset(u8* data, size_t size, size_t assetIndex, const u8** manifestCursor,
                                           const u8* manifestEnd, const u8* permutations,
                                           size_t permutationCount) {
    const u8* cursor = *manifestCursor;
    u32 manifestIndex;
    u32 manifestSize;
    u32 payloadSize;
    u32 compressedSize;
    uLongf inflatedSize;
    u8* payload = NULL;
    const u8* selectors;
    const u8* mappings;
    const u8* mappingEnd;
    u8* mappedBytes = NULL;
    size_t mappedSize = 0;
    size_t mappedCursor = 0;
    size_t selectorCount = (size + 7) / 8;
    size_t blockIndex;
    s32 ok = false;

    if ((size_t)(manifestEnd - cursor) < 16) {
        return false;
    }

    manifestIndex = OotPs2AssetSetup_ReadLe32(cursor + 0);
    manifestSize = OotPs2AssetSetup_ReadLe32(cursor + 4);
    payloadSize = OotPs2AssetSetup_ReadLe32(cursor + 8);
    compressedSize = OotPs2AssetSetup_ReadLe32(cursor + 12);
    cursor += 16;

    if ((manifestIndex != assetIndex) || (manifestSize != size) || (payloadSize < selectorCount) ||
        ((size_t)(manifestEnd - cursor) < compressedSize)) {
        return false;
    }

    payload = malloc(payloadSize);
    if (payload == NULL) {
        return false;
    }

    inflatedSize = payloadSize;
    if ((uncompress(payload, &inflatedSize, cursor, compressedSize) != Z_OK) || (inflatedSize != payloadSize)) {
        goto cleanup;
    }

    cursor += compressedSize;
    selectors = payload;
    mappings = selectors + selectorCount;
    mappingEnd = payload + payloadSize;

    for (blockIndex = 0; blockIndex < selectorCount; blockIndex++) {
        if (selectors[blockIndex] == OOT_PS2_TRANSFORM_MAPPED_SELECTOR) {
            size_t offset = blockIndex * 8;

            mappedSize += (size - offset) < 8 ? size - offset : 8;
        }
    }

    if ((size_t)(mappingEnd - mappings) != mappedSize * 5) {
        goto cleanup;
    }

    if (mappedSize != 0) {
        size_t i;

        mappedBytes = malloc(mappedSize);
        if (mappedBytes == NULL) {
            goto cleanup;
        }

        for (i = 0; i < mappedSize; i++) {
            u32 sourceOffset = OotPs2AssetSetup_ReadLe32(mappings);
            u8 delta = mappings[4];

            mappings += 5;
            if (sourceOffset >= size) {
                goto cleanup;
            }
            mappedBytes[i] = data[sourceOffset] + delta;
        }
    }

    for (blockIndex = 0; blockIndex < selectorCount; blockIndex++) {
        size_t offset = blockIndex * 8;
        size_t blockSize = (size - offset) < 8 ? size - offset : 8;
        u8 selector = selectors[blockIndex];
        u8 source[8];
        size_t i;

        memcpy(source, data + offset, blockSize);

        if (selector < permutationCount) {
            const u8* mapping = permutations + (selector * 8);

            if (blockSize != 8) {
                goto cleanup;
            }

            for (i = 0; i < 8; i++) {
                data[offset + i] = source[mapping[i]];
            }
        } else if (selector == OOT_PS2_TRANSFORM_ZERO_SELECTOR) {
            memset(data + offset, 0, blockSize);
        } else if (selector == OOT_PS2_TRANSFORM_MAPPED_SELECTOR) {
            if ((mappedSize - mappedCursor) < blockSize) {
                goto cleanup;
            }
            memcpy(data + offset, mappedBytes + mappedCursor, blockSize);
            mappedCursor += blockSize;
        } else {
            goto cleanup;
        }
    }

    if ((mappings != mappingEnd) || (mappedCursor != mappedSize)) {
        goto cleanup;
    }

    *manifestCursor = cursor;
    ok = true;

cleanup:
    free(mappedBytes);
    free(payload);
    return ok;
}

static s32 OotPs2AssetSetup_Build(OotPs2Handle romFd, const char* outputPath, const char* tempPath) {
    const size_t compressedSize = gOotPortAssetTransformCompressedEnd - gOotPortAssetTransformCompressed;
    const u8* compressed = gOotPortAssetTransformCompressed;
    const u8* manifestCursor;
    const u8* manifestEnd;
    const u8* permutations;
    u32 manifestEntryCount;
    u32 permutationCount;
    u32 nativeSeen = 0;
    OotPs2DmaEntry* dmaEntries = NULL;
    OotPs2Handle outputFd = -1;
    size_t assetIndex;
    s32 ok = false;

    if ((compressedSize < 12) || (memcmp(compressed, "OPZ4", 4) != 0)) {
        OotPs2AssetSetup_ShowError("Conversion data is missing");
        return false;
    }

    OotPs2AssetSetup_ShowProgress(100, "Preparing conversion data");
    manifestEntryCount = OotPs2AssetSetup_ReadLe32(compressed + 4);
    permutationCount = OotPs2AssetSetup_ReadLe32(compressed + 8);

    if ((permutationCount != OOT_PS2_TRANSFORM_ZERO_SELECTOR) ||
        (compressedSize < 12 + (permutationCount * 8))) {
        OotPs2AssetSetup_ShowError("Conversion data is incompatible");
        return false;
    }

    permutations = compressed + 12;
    manifestCursor = permutations + (permutationCount * 8);
    manifestEnd = compressed + compressedSize;

    dmaEntries = malloc(sizeof(*dmaEntries) * OOT_PS2_DMADATA_COUNT);
    if (dmaEntries == NULL) {
        OotPs2AssetSetup_ShowError("Not enough memory for asset setup");
        goto cleanup;
    }

    if (!OotPs2AssetSetup_LoadDmaTable(romFd, dmaEntries)) {
        OotPs2AssetSetup_ShowError("Could not read the ROM file table");
        goto cleanup;
    }

    OotPs2File_Remove(tempPath);
    outputFd = OotPs2File_Open(tempPath, OOT_PS2_FILE_WRONLY | OOT_PS2_FILE_CREAT | OOT_PS2_FILE_TRUNC, 0777);
    if (outputFd < 0) {
        OotPs2AssetSetup_ShowError("Could not create the asset file");
        goto cleanup;
    }

    for (assetIndex = 0; assetIndex < gOotPortExternalAssetCount; assetIndex++) {
        const OotPortExternalAsset* asset = &gOotPortExternalAssets[assetIndex];
        const OotPs2DmaEntry* dma = OotPs2AssetSetup_FindDmaEntry(dmaEntries, asset);
        size_t assetSize = asset->vromEnd - asset->vromStart;
        u8* data = NULL;
        char status[64];

        if ((assetIndex & 7) == 0) {
            snprintf(status, sizeof(status), "Converting assets: %lu / %lu", (unsigned long)assetIndex,
                     (unsigned long)gOotPortExternalAssetCount);
            OotPs2AssetSetup_ShowProgress(100 + (u32)((assetIndex * 900) / gOotPortExternalAssetCount), status);
        }

        if (dma == NULL) {
            if ((strcmp(asset->name, "bump_texture_static") == 0) && (assetSize == 0x400)) {
                data = calloc(1, assetSize);
                if (data == NULL) {
                    OotPs2AssetSetup_ShowError("Not enough memory for asset setup");
                    goto cleanup;
                }
            } else {
                snprintf(status, sizeof(status), "Could not extract %.36s", asset->name);
                OotPs2AssetSetup_ShowError(status);
                goto cleanup;
            }
        }

        if ((data == NULL) && (asset->flags == 0) && (dma->romEnd == 0)) {
            u32 assetRomOffset = dma->romStart + (u32)(asset->originalVromStart - dma->vromStart);

            if (!OotPs2AssetSetup_CopyUncompressedAsset(romFd, outputFd, assetRomOffset, asset->fileOffset,
                                                         assetSize)) {
                OotPs2AssetSetup_ShowError("Could not copy data from the ROM");
                goto cleanup;
            }
        } else {
            if ((data == NULL) && !OotPs2AssetSetup_LoadAsset(romFd, dma, asset, &data, assetSize)) {
                snprintf(status, sizeof(status), "Could not extract %.36s", asset->name);
                OotPs2AssetSetup_ShowError(status);
                goto cleanup;
            }

            if ((asset->flags != 0) &&
                !OotPs2AssetSetup_TransformAsset(data, assetSize, assetIndex, &manifestCursor, manifestEnd,
                                                 permutations, permutationCount)) {
                snprintf(status, sizeof(status), "Could not convert %.36s", asset->name);
                OotPs2AssetSetup_ShowError(status);
                free(data);
                goto cleanup;
            }

            if (!OotPs2AssetSetup_WriteAt(outputFd, asset->fileOffset, data, assetSize)) {
                OotPs2AssetSetup_ShowError("Could not write the asset file");
                free(data);
                goto cleanup;
            }

            free(data);
        }

        nativeSeen += asset->flags != 0;

        if ((assetIndex + 1) == gOotPortExternalAssetCount) {
            snprintf(status, sizeof(status), "Converting assets: %lu / %lu", (unsigned long)(assetIndex + 1),
                     (unsigned long)gOotPortExternalAssetCount);
            OotPs2AssetSetup_ShowProgress(1000, status);
        }
    }

    if ((nativeSeen != manifestEntryCount) || (manifestCursor != manifestEnd)) {
        OotPs2AssetSetup_ShowError("Conversion data did not match the ROM");
        goto cleanup;
    }

    OotPs2File_Close(outputFd);
    outputFd = -1;
    OotPs2File_Remove(outputPath);

    if (OotPs2File_Rename(tempPath, outputPath) < 0) {
        OotPs2AssetSetup_ShowError("Could not finish the asset file");
        goto cleanup;
    }

    ok = true;

cleanup:
    if (outputFd >= 0) {
        OotPs2File_Close(outputFd);
    }
    if (!ok) {
        OotPs2File_Remove(tempPath);
    }
    free(dmaEntries);
    return ok;
}

s32 OotPs2AssetSetup_Ensure(void) {
    char outputBuffer[384];
    char tempBuffer[384];
    char romBuffer[384];
    const char* outputPath = OotPort_ResolveRootPath(OOT_PS2_PACKED_ASSET_PATH, outputBuffer, sizeof(outputBuffer));
    const char* tempPath = OotPort_ResolveRootPath(OOT_PS2_PACKED_ASSET_TEMP_PATH, tempBuffer, sizeof(tempBuffer));
    size_t expectedSize;
    OotPs2Handle romFd;
    s32 ok;

    if (gOotPortExternalAssetCount == 0) {
        return false;
    }

    expectedSize = gOotPortExternalAssets[gOotPortExternalAssetCount - 1].fileOffset +
                   (gOotPortExternalAssets[gOotPortExternalAssetCount - 1].vromEnd -
                    gOotPortExternalAssets[gOotPortExternalAssetCount - 1].vromStart);

    if (OotPs2AssetSetup_FileHasSize(outputPath, expectedSize)) {
        return true;
    }

    sOotPs2AssetSetupProgress = 0;
    sOotPs2AssetSetupErrorShown = false;
    OotPs2AssetSetup_ShowProgress(0, "Checking NTSC 1.0 ROM");

    romFd = OotPs2AssetSetup_OpenRom(romBuffer, sizeof(romBuffer));
    if (romFd < 0) {
        OotPs2AssetSetup_ShowError("baserom.z64 not found");
        return false;
    }

    ok = OotPs2AssetSetup_ValidateRom(romFd) && OotPs2AssetSetup_Build(romFd, outputPath, tempPath);
    OotPs2File_Close(romFd);

    if (ok) {
        OotPs2AssetSetup_ShowProgress(1000, "Asset setup complete");
    } else if (!sOotPs2AssetSetupErrorShown) {
        OotPs2AssetSetup_ShowError("Asset setup failed");
    }

    return ok;
}
