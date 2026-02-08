/*
 * LZ4 compression wrapper for Tracegrind.
 * Uses vendored LZ4 library adapted for Valgrind (no libc).
 */

#ifndef TG_LZ4_H
#define TG_LZ4_H

#include "pub_tool_basics.h"

/* Return the maximum compressed size for a given source length */
SizeT tg_lz4_compress_bound(SizeT src_size);

/* Compress src[0..src_size-1] into dst.
 * dst_capacity must be >= tg_lz4_compress_bound(src_size).
 * Returns the compressed size on success, 0 on error.
 */
SizeT tg_lz4_compress(void*       dst,
                      SizeT       dst_capacity,
                      const void* src,
                      SizeT       src_size);

#endif /* TG_LZ4_H */
