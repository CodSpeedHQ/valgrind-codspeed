/*
 * Minimal MsgPack encoder for Tracegrind.
 * Write-only, adapted for Valgrind (no libc).
 *
 * MsgPack format spec: https://github.com/msgpack/msgpack/blob/master/spec.md
 */

#include "pub_tool_basics.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_mallocfree.h"

#include "tg_msgpack.h"

/* Ensure at least `needed` bytes of capacity */
static void msgpack_ensure(msgpack_buffer* mb, Int needed)
{
    if (mb->size + needed <= mb->capacity)
        return;
    Int new_cap = mb->capacity * 2;
    if (new_cap < mb->size + needed)
        new_cap = mb->size + needed;
    mb->data = VG_(realloc)("tg.msgpack.buf", mb->data, new_cap);
    mb->capacity = new_cap;
}

static void write_byte(msgpack_buffer* mb, UChar b)
{
    msgpack_ensure(mb, 1);
    mb->data[mb->size++] = b;
}

static void write_bytes(msgpack_buffer* mb, const void* data, Int len)
{
    msgpack_ensure(mb, len);
    VG_(memcpy)(mb->data + mb->size, data, len);
    mb->size += len;
}

/* Write big-endian integers */
static void write_be16(msgpack_buffer* mb, UShort val)
{
    UChar buf[2];
    buf[0] = (UChar)(val >> 8);
    buf[1] = (UChar)(val);
    write_bytes(mb, buf, 2);
}

static void write_be32(msgpack_buffer* mb, UInt val)
{
    UChar buf[4];
    buf[0] = (UChar)(val >> 24);
    buf[1] = (UChar)(val >> 16);
    buf[2] = (UChar)(val >> 8);
    buf[3] = (UChar)(val);
    write_bytes(mb, buf, 4);
}

static void write_be64(msgpack_buffer* mb, ULong val)
{
    UChar buf[8];
    buf[0] = (UChar)(val >> 56);
    buf[1] = (UChar)(val >> 48);
    buf[2] = (UChar)(val >> 40);
    buf[3] = (UChar)(val >> 32);
    buf[4] = (UChar)(val >> 24);
    buf[5] = (UChar)(val >> 16);
    buf[6] = (UChar)(val >> 8);
    buf[7] = (UChar)(val);
    write_bytes(mb, buf, 8);
}

void msgpack_init(msgpack_buffer* mb, Int capacity)
{
    if (capacity < 256) capacity = 256;
    mb->data = VG_(malloc)("tg.msgpack.init", capacity);
    mb->size = 0;
    mb->capacity = capacity;
}

void msgpack_free(msgpack_buffer* mb)
{
    if (mb->data) {
        VG_(free)(mb->data);
        mb->data = NULL;
    }
    mb->size = 0;
    mb->capacity = 0;
}

void msgpack_reset(msgpack_buffer* mb)
{
    mb->size = 0;
}

void msgpack_write_nil(msgpack_buffer* mb)
{
    write_byte(mb, 0xc0);
}

void msgpack_write_bool(msgpack_buffer* mb, Bool val)
{
    write_byte(mb, val ? 0xc3 : 0xc2);
}

void msgpack_write_int(msgpack_buffer* mb, Long val)
{
    if (val >= 0) {
        msgpack_write_uint(mb, (ULong)val);
    } else if (val >= -32) {
        /* negative fixint: 111xxxxx */
        write_byte(mb, (UChar)(val & 0xff));
    } else if (val >= -128) {
        write_byte(mb, 0xd0); /* int8 */
        write_byte(mb, (UChar)(val & 0xff));
    } else if (val >= -32768) {
        write_byte(mb, 0xd1); /* int16 */
        write_be16(mb, (UShort)(val & 0xffff));
    } else if (val >= -2147483648LL) {
        write_byte(mb, 0xd2); /* int32 */
        write_be32(mb, (UInt)(val & 0xffffffff));
    } else {
        write_byte(mb, 0xd3); /* int64 */
        write_be64(mb, (ULong)val);
    }
}

void msgpack_write_uint(msgpack_buffer* mb, ULong val)
{
    if (val <= 0x7f) {
        /* positive fixint: 0xxxxxxx */
        write_byte(mb, (UChar)val);
    } else if (val <= 0xff) {
        write_byte(mb, 0xcc); /* uint8 */
        write_byte(mb, (UChar)val);
    } else if (val <= 0xffff) {
        write_byte(mb, 0xcd); /* uint16 */
        write_be16(mb, (UShort)val);
    } else if (val <= 0xffffffff) {
        write_byte(mb, 0xce); /* uint32 */
        write_be32(mb, (UInt)val);
    } else {
        write_byte(mb, 0xcf); /* uint64 */
        write_be64(mb, val);
    }
}

void msgpack_write_str(msgpack_buffer* mb, const HChar* str, Int len)
{
    if (len < 0) len = VG_(strlen)(str);

    if (len <= 31) {
        /* fixstr: 101xxxxx */
        write_byte(mb, (UChar)(0xa0 | len));
    } else if (len <= 0xff) {
        write_byte(mb, 0xd9); /* str8 */
        write_byte(mb, (UChar)len);
    } else if (len <= 0xffff) {
        write_byte(mb, 0xda); /* str16 */
        write_be16(mb, (UShort)len);
    } else {
        write_byte(mb, 0xdb); /* str32 */
        write_be32(mb, (UInt)len);
    }
    write_bytes(mb, str, len);
}

void msgpack_write_bin(msgpack_buffer* mb, const UChar* data, Int len)
{
    if (len <= 0xff) {
        write_byte(mb, 0xc4); /* bin8 */
        write_byte(mb, (UChar)len);
    } else if (len <= 0xffff) {
        write_byte(mb, 0xc5); /* bin16 */
        write_be16(mb, (UShort)len);
    } else {
        write_byte(mb, 0xc6); /* bin32 */
        write_be32(mb, (UInt)len);
    }
    write_bytes(mb, data, len);
}

void msgpack_write_array_header(msgpack_buffer* mb, UInt count)
{
    if (count <= 15) {
        /* fixarray: 1001xxxx */
        write_byte(mb, (UChar)(0x90 | count));
    } else if (count <= 0xffff) {
        write_byte(mb, 0xdc); /* array16 */
        write_be16(mb, (UShort)count);
    } else {
        write_byte(mb, 0xdd); /* array32 */
        write_be32(mb, count);
    }
}

void msgpack_write_map_header(msgpack_buffer* mb, UInt count)
{
    if (count <= 15) {
        /* fixmap: 1000xxxx */
        write_byte(mb, (UChar)(0x80 | count));
    } else if (count <= 0xffff) {
        write_byte(mb, 0xde); /* map16 */
        write_be16(mb, (UShort)count);
    } else {
        write_byte(mb, 0xdf); /* map32 */
        write_be32(mb, count);
    }
}

void msgpack_write_key(msgpack_buffer* mb, const HChar* key)
{
    msgpack_write_str(mb, key, -1);
}
