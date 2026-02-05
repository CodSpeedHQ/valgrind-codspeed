/*
 * Minimal MsgPack encoder for Tracegrind.
 * Write-only, adapted for Valgrind (no libc).
 */

#ifndef TG_MSGPACK_H
#define TG_MSGPACK_H

#include "pub_tool_basics.h"

typedef struct {
    UChar* data;
    Int    size;
    Int    capacity;
} msgpack_buffer;

void msgpack_init(msgpack_buffer* mb, Int capacity);
void msgpack_free(msgpack_buffer* mb);
void msgpack_reset(msgpack_buffer* mb);

/* Encode primitives */
void msgpack_write_nil(msgpack_buffer* mb);
void msgpack_write_bool(msgpack_buffer* mb, Bool val);
void msgpack_write_int(msgpack_buffer* mb, Long val);
void msgpack_write_uint(msgpack_buffer* mb, ULong val);
void msgpack_write_str(msgpack_buffer* mb, const HChar* str, Int len);
void msgpack_write_bin(msgpack_buffer* mb, const UChar* data, Int len);

/* Containers */
void msgpack_write_array_header(msgpack_buffer* mb, UInt count);
void msgpack_write_map_header(msgpack_buffer* mb, UInt count);

/* Convenience: write a string key (for maps) */
void msgpack_write_key(msgpack_buffer* mb, const HChar* key);

#endif /* TG_MSGPACK_H */
