// SPDX-License-Identifier: Zlib
//
// Copyright (c) 2024 Antonio Niño Díaz

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nds/arm9/grf.h>
#include <nds/decompress.h>

// General file structure:
//
// "RIFF" # {
//     "GRF " # {
//         "HDR " # { header info }
//         "GFX " # { gfx data }
//         "MAP " # { map data }
//         "MTIL" # { metatile data }
//         "MMAP" # { metamap data }
//         "PAL " # { palette data }
//     }
// }
//
// The only mandatory chunk inside the "GRF " is "HDR ".

typedef struct
{
    uint32_t    id;
    uint32_t    size;
    uint8_t     data[];
} RIFFChunk;

#define CHUNK_ID(a, b, c, d) \
    ((uint32_t)((a) | ((b) << 8) | ((c) << 16) | ((d) << 24)))

#define ID_RIFF     CHUNK_ID('R', 'I', 'F', 'F')
#define ID_GRF      CHUNK_ID('G', 'R', 'F', ' ')
#define ID_HDR      CHUNK_ID('H', 'D', 'R', ' ')
#define ID_GFX      CHUNK_ID('G', 'F', 'X', ' ')
#define ID_MAP      CHUNK_ID('M', 'A', 'P', ' ')
#define ID_MTIL     CHUNK_ID('M', 'T', 'I', 'L')
#define ID_MMAP     CHUNK_ID('M', 'M', 'A', 'P')
#define ID_PAL      CHUNK_ID('P', 'A', 'L', ' ')

// Extracts a GRF item
static GRFError grfExtract(const void *src, void **dst, size_t *sz)
{
    if ((src == NULL) || (dst == NULL))
        return GRF_NULL_POINTER;

    // The header of this data is the header used for all GBA/NDS BIOS
    // decompression routines. Uncompressed chunks also use the same format for
    // consistency.
    uint32_t header = *(const uint32_t *)src;
    uint32_t size = header >> 8;

    if (sz != NULL)
        *sz = size;

    // If the user has already provided a pointer, use it. If not, allocate mem
    if (*dst == NULL)
    {
        *dst = malloc(size);
        if (*dst == NULL)
            return GRF_NOT_ENOUGH_MEMORY;
    }

    switch (header & 0xF0)
    {
        case 0x00: // No compression
            swiCopy((const uint8_t *)src + 4, *dst,
                    COPY_MODE_HWORD | COPY_MODE_COPY | (size >> 1));
            return GRF_NO_ERROR;
        case 0x10: // LZ77
            decompress(src, *dst, LZ77Vram);
            return GRF_NO_ERROR;
        case 0x20: // Huffman
            decompress(src, *dst, HUFF);
            return GRF_NO_ERROR;
        case 0x30: // RLE
            decompress(src, *dst, RLEVram);
            return GRF_NO_ERROR;
        default:
            return GRF_UNKNOWN_COMPRESSION;
    }
}

GRFError grfLoadMemEx(const void *src, GRFHeader *header,
                      void **gfxDst, size_t *gfxSize,
                      void **mapDst, size_t *mapSize,
                      void **palDst, size_t *palSize,
                      void **mtilDst, size_t *mtilSize,
                      void **mmapDst, size_t *mmapSize)
{
    if (src == NULL)
        return GRF_NULL_POINTER;

    const RIFFChunk *riff_chunk = src;

    if (riff_chunk->id != ID_RIFF)
        return GRF_INVALID_ID_RIFF;

    uint32_t riff_size = riff_chunk->size;

    const RIFFChunk *grf_chunk = (const RIFFChunk *)&(riff_chunk->data[0]);

    if (grf_chunk->id != ID_GRF)
        return GRF_INVALID_ID_GRF;

    uint32_t grf_size = grf_chunk->size;

    // Ensure that both sizes are consistent
    if (riff_size != grf_size + 8)
        return GRF_INCONSISTENT_SIZES;

    uintptr_t ptr = (uintptr_t)&(grf_chunk->data[0]);
    uintptr_t end = (uintptr_t)src + riff_size + 8;

    while (ptr < end)
    {
        const RIFFChunk *chunk = (const RIFFChunk *)ptr;

        uint32_t id = chunk->id;
        uint32_t size = chunk->size;
        const void *data = (const void *)&(chunk->data[0]);

        ptr += size + 8;

        GRFError ret = GRF_NO_ERROR;

        switch (id)
        {
            case ID_HDR:
                if (size != sizeof(GRFHeader))
                    return GRF_INCONSISTENT_SIZES;
                memcpy(header, data, size);
                break;
            case ID_GFX:
                if (gfxDst)
                    ret = grfExtract(data, gfxDst, gfxSize);
                break;
            case ID_MAP:
                if (mapDst)
                    ret = grfExtract(data, mapDst, mapSize);
                break;
            case ID_MTIL:
                if (mtilDst)
                    ret = grfExtract(data, mtilDst, mtilSize);
                break;
            case ID_MMAP:
                if (mmapDst)
                    ret = grfExtract(data, mmapDst, mmapSize);
                break;
            case ID_PAL:
                if (palDst)
                    ret = grfExtract(data, palDst, palSize);
                break;
            default:
                // Ignore unknown chunks rather than failing
                break;
        }

        if (ret != GRF_NO_ERROR)
            return ret;
    }

    return GRF_NO_ERROR;
}

GRFError grfLoadMem(const void *src, GRFHeader *header,
                    void **gfxDst, size_t *gfxSize,
                    void **mapDst, size_t *mapSize,
                    void **palDst, size_t *palSize)
{
    return grfLoadMemEx(src, header, gfxDst, gfxSize, mapDst, mapSize,
                        palDst, palSize, NULL, NULL, NULL, NULL);
}

static void *grfReadAllFile(FILE *file)
{
    if (file == NULL)
        return NULL;

    if (fseek(file, 0, SEEK_END) != 0)
        return NULL;

    size_t size = ftell(file);
    rewind(file);

    char *buffer = malloc(size);
    if (buffer == NULL)
        return NULL;

    if (fread(buffer, 1, size, file) != size)
    {
        free(buffer);
        return NULL;
    }

    return buffer;
}

GRFError grfLoadFileEx(FILE *file, GRFHeader *header,
                       void **gfxDst, size_t *gfxSize,
                       void **mapDst, size_t *mapSize,
                       void **palDst, size_t *palSize,
                       void **mtilDst, size_t *mtilSize,
                       void **mmapDst, size_t *mmapSize)
{
    if (file == NULL)
        return GRF_NULL_POINTER;

    void *src = grfReadAllFile(file);
    if (src == NULL)
        return GRF_FILE_NOT_READ;

    GRFError ret = grfLoadMemEx(src, header, gfxDst, gfxSize, mapDst, mapSize,
                                palDst, palSize, mtilDst, mtilSize,
                                mmapDst, mmapSize);

    free(src);

    return ret;
}

GRFError grfLoadFile(FILE *file, GRFHeader *header,
                     void **gfxDst, size_t *gfxSize,
                     void **mapDst, size_t *mapSize,
                     void **palDst, size_t *palSize)
{
    return grfLoadFileEx(file, header, gfxDst, gfxSize, mapDst, mapSize,
                         palDst, palSize, NULL, NULL, NULL, NULL);
}

GRFError grfLoadPathEx(const char *path, GRFHeader *header,
                       void **gfxDst, size_t *gfxSize,
                       void **mapDst, size_t *mapSize,
                       void **palDst, size_t *palSize,
                       void **mtilDst, size_t *mtilSize,
                       void **mmapDst, size_t *mmapSize)
{
    if (path == NULL)
        return GRF_NULL_POINTER;

    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return GRF_FILE_NOT_OPENED;

    GRFError ret = grfLoadFileEx(file, header, gfxDst, gfxSize, mapDst, mapSize,
                                 palDst, palSize, mtilDst, mtilSize,
                                 mmapDst, mmapSize);

    if (fclose(file) != 0)
        return GRF_FILE_NOT_CLOSED;

    return ret;
}

GRFError grfLoadPath(const char *path, GRFHeader *header,
                     void **gfxDst, size_t *gfxSize,
                     void **mapDst, size_t *mapSize,
                     void **palDst, size_t *palSize)
{
    return grfLoadPathEx(path, header, gfxDst, gfxSize, mapDst, mapSize,
                         palDst, palSize, NULL, NULL, NULL, NULL);
}
