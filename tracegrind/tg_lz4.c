/*
 * LZ4 compression wrapper for Tracegrind.
 * Uses vendored LZ4 library adapted for Valgrind (no libc).
 *
 * BSD 2-Clause License - see lz4.c for full license.
 */

#include "pub_tool_basics.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_mallocfree.h"

#include "tg_lz4.h"

/*------------------------------------------------------------*/
/*--- LZ4 Configuration for Valgrind                       ---*/
/*------------------------------------------------------------*/

/* Disable memory allocation functions (we provide them below) */
#define LZ4_USER_MEMORY_FUNCTIONS 1

/* Freestanding mode - no string.h */
#define LZ4_FREESTANDING 1

/* Provide size_t */
#ifndef size_t
#define size_t SizeT
#endif

/* Provide INT_MAX from limits.h */
#ifndef INT_MAX
#define INT_MAX 2147483647
#endif

#ifndef UINT_MAX
#define UINT_MAX 4294967295U
#endif

/*------------------------------------------------------------*/
/*--- Memory function replacements                         ---*/
/*------------------------------------------------------------*/

/* Define LZ4_memcpy, LZ4_memmove, LZ4_memset before including lz4 */
#define LZ4_memcpy(dst, src, size)  VG_(memcpy)((dst), (src), (size))
#define LZ4_memmove(dst, src, size) VG_(memmove)((dst), (src), (size))
#define LZ4_memset(p, v, s)         VG_(memset)((p), (v), (s))

/*------------------------------------------------------------*/
/*--- Memory allocation functions (LZ4_USER_MEMORY_FUNCTIONS) */
/*------------------------------------------------------------*/

void* LZ4_malloc(size_t s) { return VG_(malloc)("tg.lz4", s); }

void* LZ4_calloc(size_t n, size_t s) { return VG_(calloc)("tg.lz4", n, s); }

void LZ4_free(void* p)
{
   if (p)
      VG_(free)(p);
}

/*------------------------------------------------------------*/
/*--- Include the original LZ4 implementation              ---*/
/*------------------------------------------------------------*/

/* Disable assert (LZ4 has its own fallback) */
#define LZ4_DEBUG 0

/* Include the main LZ4 source */
#include "lz4.c"

/*------------------------------------------------------------*/
/*--- Wrapper API                                          ---*/
/*------------------------------------------------------------*/

SizeT tg_lz4_compress_bound(SizeT src_size)
{
   return LZ4_compressBound((int)src_size);
}

SizeT tg_lz4_compress(void*       dst,
                      SizeT       dst_capacity,
                      const void* src,
                      SizeT       src_size)
{
   int result = LZ4_compress_fast((const char*)src, (char*)dst, (int)src_size,
                                  (int)dst_capacity, 4 /* acceleration */);
   if (result <= 0) {
      return 0;
   }
   return (SizeT)result;
}
