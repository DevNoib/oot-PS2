#include "oot_ps2_runtime_patch.h"
#include "oot_port_asset_loader.h"
#include <kernel.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define PATCH_ZERO_SELECTOR 56U
#define PATCH_MAPPED_SELECTOR 57U
#define PATCH_TEXTURE_WORDS (1U << 0)

extern const u8 gOotPs2RuntimePatchBlob[] __attribute__((weak));
extern const u8 gOotPs2RuntimePatchBlobEnd[] __attribute__((weak));
extern u8 _ftext[];

static u32 ReadU32(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static s32 ReadS32(const u8* p) { return (s32)ReadU32(p); }

static s32 ApplyTransform(u8* data, size_t size, const u8* payload, size_t payloadSize,
                          const u8* permutations, size_t permutationCount,
                          u8* mappedScratch, size_t mappedScratchSize) {
    const size_t selectorCount = (size + 7U) / 8U;
    const u8* selectors = payload;
    const u8* mappings;
    const u8* mappingEnd = payload + payloadSize;
    u8* mapped = mappedScratch;
    size_t mappedSize = 0;
    size_t mappedCursor = 0;
    size_t block;
    s32 ok = false;

    if (payloadSize < selectorCount) return false;
    mappings = selectors + selectorCount;
    for (block = 0; block < selectorCount; block++) {
        if (selectors[block] == PATCH_MAPPED_SELECTOR) {
            const size_t off = block * 8U;
            mappedSize += ((size - off) < 8U) ? (size - off) : 8U;
        }
    }
    if ((size_t)(mappingEnd - mappings) != mappedSize * 5U) return false;
    if (mappedSize != 0) {
        size_t i;
        if (mapped == NULL || mappedSize > mappedScratchSize) return false;
        for (i = 0; i < mappedSize; i++) {
            const u32 sourceOffset = ReadU32(mappings);
            const u8 delta = mappings[4];
            mappings += 5;
            if (sourceOffset >= size) goto done;
            mapped[i] = data[sourceOffset] + delta;
        }
    }
    for (block = 0; block < selectorCount; block++) {
        const size_t off = block * 8U;
        const size_t blockSize = ((size - off) < 8U) ? (size - off) : 8U;
        const u8 selector = selectors[block];
        u8 source[8];
        size_t i;
        memcpy(source, data + off, blockSize);
        if (selector < permutationCount) {
            const u8* map = permutations + selector * 8U;
            if (blockSize != 8U) goto done;
            for (i = 0; i < 8U; i++) data[off + i] = source[map[i]];
        } else if (selector == PATCH_ZERO_SELECTOR) {
            memset(data + off, 0, blockSize);
        } else if (selector == PATCH_MAPPED_SELECTOR) {
            if (mappedSize - mappedCursor < blockSize) goto done;
            memcpy(data + off, mapped + mappedCursor, blockSize);
            mappedCursor += blockSize;
        } else {
            goto done;
        }
    }
    ok = (mappings == mappingEnd) && (mappedCursor == mappedSize);
done:
    return ok;
}

typedef struct OotPs2RuntimePatchRecord {
    u32 destinationOffset;
    u32 sourceVrom;
    u32 size;
    u32 payloadSize;
    u32 compressedSize;
    u32 relocationCount;
    u32 flags;
    const u8* compressed;
    const u8* relocations;
} OotPs2RuntimePatchRecord;

static void OotPs2RuntimePatch_SortReadOrder(u32* order, const OotPs2RuntimePatchRecord* records, u32 count) {
    u32 i;

    for (i = 1; i < count; i++) {
        const u32 value = order[i];
        const uintptr_t valueVrom = records[value].sourceVrom;
        u32 j = i;

        while (j != 0) {
            const u32 previous = order[j - 1];
            if ((records[previous].sourceVrom < valueVrom) ||
                ((records[previous].sourceVrom == valueVrom) && (previous < value))) {
                break;
            }
            order[j] = previous;
            j--;
        }
        order[j] = value;
    }
}

s32 OotPs2RuntimePatch_Apply(void) {
    const u8* cursor = gOotPs2RuntimePatchBlob;
    const u8* end = gOotPs2RuntimePatchBlobEnd;
    const u8* permutations;
    OotPs2RuntimePatchRecord* records = NULL;
    u32* readOrder = NULL;
    u32 patchCount;
    u32 permutationCount;
    u32 patchIndex;
    size_t maxPayloadSize = 0;
    size_t maxRecordSize = 0;
    u8* payloadScratch = NULL;
    u8* mappedScratch = NULL;
    s32 hasFlags;
    s32 ok = false;

    if ((cursor == NULL) || (end == NULL) || ((size_t)(end - cursor) < 12U)) goto done;
    if (memcmp(cursor, "OPB2", 4) == 0) hasFlags = true;
    else if (memcmp(cursor, "OPB1", 4) == 0) hasFlags = false;
    else goto done;

    patchCount = ReadU32(cursor + 4);
    permutationCount = ReadU32(cursor + 8);
    cursor += 12;
    if ((permutationCount != PATCH_ZERO_SELECTOR) || ((size_t)(end - cursor) < permutationCount * 8U)) goto done;
    permutations = cursor;
    cursor += permutationCount * 8U;

    if ((patchCount == 0) || (patchCount > 4096U)) goto done;
    records = calloc(patchCount, sizeof(*records));
    readOrder = malloc((size_t)patchCount * sizeof(*readOrder));
    if ((records == NULL) || (readOrder == NULL)) goto done;

    for (patchIndex = 0; patchIndex < patchCount; patchIndex++) {
        OotPs2RuntimePatchRecord* record = &records[patchIndex];
        const size_t headerSize = hasFlags ? 28U : 24U;
        size_t relocationBytes;

        if ((size_t)(end - cursor) < headerSize) goto done;
        record->destinationOffset = ReadU32(cursor + 0);
        record->sourceVrom = ReadU32(cursor + 4);
        record->size = ReadU32(cursor + 8);
        record->payloadSize = ReadU32(cursor + 12);
        record->compressedSize = ReadU32(cursor + 16);
        record->relocationCount = ReadU32(cursor + 20);
        record->flags = hasFlags ? ReadU32(cursor + 24) : 0;
        cursor += headerSize;
        if ((record->flags & PATCH_TEXTURE_WORDS) == 0) {
            if (record->payloadSize > maxPayloadSize) maxPayloadSize = record->payloadSize;
            if (record->size > maxRecordSize) maxRecordSize = record->size;
        }

        if (record->relocationCount > (SIZE_MAX / 8U)) goto done;
        relocationBytes = (size_t)record->relocationCount * 8U;
        if (((size_t)(end - cursor) < record->compressedSize) ||
            ((size_t)(end - cursor - record->compressedSize) < relocationBytes)) goto done;

        record->compressed = cursor;
        cursor += record->compressedSize;
        record->relocations = cursor;
        cursor += relocationBytes;
        readOrder[patchIndex] = patchIndex;
    }
    if (cursor != end) goto done;
    payloadScratch = malloc(maxPayloadSize != 0 ? maxPayloadSize : 1U);
    mappedScratch = malloc(maxRecordSize != 0 ? maxRecordSize : 1U);
    if ((payloadScratch == NULL) || (mappedScratch == NULL)) goto done;

    OotPs2RuntimePatch_SortReadOrder(readOrder, records, patchCount);

    for (patchIndex = 0; patchIndex < patchCount; patchIndex++) {
        const u32 recordIndex = readOrder[patchIndex];
        const OotPs2RuntimePatchRecord* record = &records[recordIndex];
        u8* destination = _ftext + record->destinationOffset;

        if (OotPort_AssetRead(destination, record->sourceVrom, record->size) != OOT_PORT_ASSET_READ_OK) goto done;
    }

    for (patchIndex = 0; patchIndex < patchCount; patchIndex++) {
        const OotPs2RuntimePatchRecord* record = &records[patchIndex];
        u8* destination = _ftext + record->destinationOffset;
        u8* payload;
        uLongf inflatedSize;
        u32 relocationIndex;
        const u8* relocationCursor = record->relocations;

        if ((record->flags & PATCH_TEXTURE_WORDS) != 0) {
            continue;
        }

        if ((patchIndex & 31U) == 0U || patchIndex == 334U) {
        }

        payload = payloadScratch;
        inflatedSize = record->payloadSize;
        if ((uncompress(payload, &inflatedSize, record->compressed, record->compressedSize) != Z_OK) ||
            (inflatedSize != record->payloadSize) ||
            !ApplyTransform(destination, record->size, payload, record->payloadSize, permutations, permutationCount,
                            mappedScratch, maxRecordSize)) {
            goto done;
        }

        for (relocationIndex = 0; relocationIndex < record->relocationCount; relocationIndex++) {
            const u32 relocationOffset = ReadU32(relocationCursor);
            const s32 adjustment = ReadS32(relocationCursor + 4);
            u32* value;
            relocationCursor += 8;
            if ((relocationOffset > record->size) || ((record->size - relocationOffset) < sizeof(*value))) goto done;
            value = (u32*)(destination + relocationOffset);
            *value += (uintptr_t)_ftext + adjustment;
        }
    }

    FlushCache(WRITEBACK_DCACHE);
    ok = true;

done:
    free(mappedScratch);
    free(payloadScratch);
    free(readOrder);
    free(records);
    return ok;
}
