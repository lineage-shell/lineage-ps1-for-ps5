/*
 * SwanStationPS5 - shelf v3: reading a few sectors of a .chd disc image (the PlayStation screen's logo data, which
 * sits on the user's own game discs). libchdr (BSD-3-Clause, third_party/swanstation/dep/libchdr) is built here as one
 * unit; its file access goes through the libretro file-stream calls, which this file supplies over stdio. Read only.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <streams/file_stream.h>

struct RFILE
{
    FILE *f;
};

RFILE *filestream_open(const char *path, unsigned mode, unsigned hints)
{
    (void)mode, (void)hints;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    RFILE *r = malloc(sizeof(*r));
    if (!r)
    {
        fclose(f);
        return NULL;
    }
    r->f = f;
    return r;
}

int64_t filestream_seek(RFILE *stream, int64_t offset, int seek_position)
{
    return fseeko(stream->f, (off_t)offset, seek_position == 2 ? SEEK_END : seek_position == 1 ? SEEK_CUR : SEEK_SET) == 0
               ? 0
               : -1;
}

int64_t filestream_read(RFILE *stream, void *data, int64_t len)
{
    return (int64_t)fread(data, 1, (size_t)len, stream->f);
}

int64_t filestream_tell(RFILE *stream)
{
    return (int64_t)ftello(stream->f);
}

int filestream_close(RFILE *stream)
{
    const int r = fclose(stream->f);
    free(stream);
    return r;
}

/* libchdr's unity build, less miniz: the app already links miniz (src/vendor.c), whose mz_ functions libchdr's zlib
 * codec reaches through miniz.h's zlib-style names */
#include "../../third_party/swanstation/dep/libchdr/deps/lzma-25.01/src/LzmaDec.c"
#include "../../third_party/swanstation/dep/libchdr/deps/zstd-1.5.7/zstddeclib.c"
#include "../../third_party/swanstation/dep/libchdr/src/libchdr_bitstream.c"
#include "../../third_party/swanstation/dep/libchdr/src/libchdr_cdrom.c"
#include "../../third_party/swanstation/dep/libchdr/src/libchdr_chd.c"
#include "../../third_party/swanstation/dep/libchdr/src/libchdr_codec_cdfl.c"
#include "../../third_party/swanstation/dep/libchdr/src/libchdr_codec_cdlz.c"
#include "../../third_party/swanstation/dep/libchdr/src/libchdr_codec_cdzl.c"
#include "../../third_party/swanstation/dep/libchdr/src/libchdr_codec_cdzs.c"
#include "../../third_party/swanstation/dep/libchdr/src/libchdr_codec_flac.c"
#include "../../third_party/swanstation/dep/libchdr/src/libchdr_codec_huff.c"
#include "../../third_party/swanstation/dep/libchdr/src/libchdr_codec_lzma.c"
#include "../../third_party/swanstation/dep/libchdr/src/libchdr_codec_zlib.c"
#include "../../third_party/swanstation/dep/libchdr/src/libchdr_codec_zstd.c"
#include "../../third_party/swanstation/dep/libchdr/src/libchdr_flac.c"
#include "../../third_party/swanstation/dep/libchdr/src/libchdr_huffman.c"

/* 2048-byte user data of `count` sectors from `lba` of the first track (raw Mode 2 or Mode 1 frames); false on any
 * error */
bool ps1_chd_read(const char *path, int lba, int count, uint8_t *out)
{
    chd_file *chd = NULL;
    if (chd_open(path, CHD_OPEN_READ, NULL, &chd) != CHDERR_NONE)
        return false;
    const chd_header *h = chd_get_header(chd);
    const uint32_t frame = 2352 + 96, per_hunk = h->hunkbytes / frame;
    uint8_t *hunk = per_hunk ? malloc(h->hunkbytes) : NULL;
    bool ok = hunk != NULL;
    uint32_t have = UINT32_MAX;
    for (int i = 0; ok && i < count; ++i)
    {
        const uint32_t f = (uint32_t)(lba + i), want = f / per_hunk;
        if (want != have)
        {
            ok = chd_read(chd, want, hunk) == CHDERR_NONE;
            have = want;
        }
        if (!ok)
            break;
        const uint8_t *s = hunk + (f % per_hunk) * frame;
        static const uint8_t SYNC[12] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
        const int off = memcmp(s, SYNC, 12) ? 0 : s[15] == 2 ? 24 : 16;
        memcpy(out + i * 2048, s + off, 2048);
    }
    free(hunk);
    chd_close(chd);
    return ok;
}
