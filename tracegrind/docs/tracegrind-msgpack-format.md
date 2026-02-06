# Tracegrind MsgPack+LZ4 Output Format

## Overview

Tracegrind produces a binary trace file combining MsgPack serialization with LZ4 block compression. Files use the `.msgpack.lz4` extension.

## File Structure

```
┌─────────────────────────────────┐
│       File Header (8 bytes)     │
├─────────────────────────────────┤
│       Schema Chunk              │
├─────────────────────────────────┤
│       Data Chunk 1..N           │
├─────────────────────────────────┤
│       End Marker (8 bytes)      │
└─────────────────────────────────┘
```

## File Header

| Offset | Size | Field   | Description |
|--------|------|---------|-------------|
| 0      | 4    | magic   | ASCII `TGMP` (0x54 0x47 0x4D 0x50) |
| 4      | 4    | version | Format version, uint32 LE (currently 2) |

## Chunk Format

Each chunk (schema and data) has the same header:

| Offset | Size | Field             | Description |
|--------|------|-------------------|-------------|
| 0      | 4    | uncompressed_size | Size after decompression, uint32 LE |
| 4      | 4    | compressed_size   | Size of LZ4 block, uint32 LE |
| 8      | N    | data              | LZ4 block-compressed MsgPack data |

## Schema Chunk

The first chunk contains a MsgPack map describing the discriminated union schema:

```json
{
    "version": 2,
    "format": "tracegrind-msgpack",
    "event_schemas": {
        "0": ["seq", "tid", "event", "fn", "obj", "file", "line", "Ir", ...],
        "1": ["seq", "tid", "event", "fn", "obj", "file", "line", "Ir", ...],
        "2": ["seq", "tid", "event", "child_pid"]
    }
}
```

### Event Types

| Type | Name  | Description |
|------|-------|-------------|
| 0    | ENTER | Function entry |
| 1    | EXIT  | Function exit |
| 2    | FORK  | Child process created |

### Row Schemas

**ENTER/EXIT rows (event 0, 1):**

| Index | Name  | Type   | Description |
|-------|-------|--------|-------------|
| 0     | seq   | uint64 | Sequence number |
| 1     | tid   | int32  | Thread ID |
| 2     | event | int    | 0 = ENTER, 1 = EXIT |
| 3     | fn    | string | Function name |
| 4     | obj   | string | Shared object path |
| 5     | file  | string | Source file path |
| 6     | line  | int32  | Line number (0 if unknown) |
| 7+    | ...   | int64  | Event counter deltas (Ir, Dr, Dw, etc.) |

**FORK rows (event 2):**

| Index | Name      | Type   | Description |
|-------|-----------|--------|-------------|
| 0     | seq       | uint64 | Sequence number |
| 1     | tid       | int32  | Thread ID that called fork |
| 2     | event     | int    | 2 = FORK |
| 3     | child_pid | int32  | PID of the new child process |

### Event Counter Columns

For ENTER/EXIT rows, event counters appear as delta values starting at index 7. Which counters are present depends on Tracegrind options:

`Ir`, `Dr`, `Dw`, `I1mr`, `D1mr`, `D1mw`, `ILmr`, `DLmr`, `DLmw`, `Bc`, `Bcm`, `Bi`, `Bim`

## Data Chunks

Each data chunk contains concatenated MsgPack arrays. The row format depends on the event type (index 2):

```
[seq, tid, 0, fn, obj, file, line, delta_Ir, ...]  # ENTER
[seq, tid, 1, fn, obj, file, line, delta_Ir, ...]  # EXIT
[seq, tid, 2, child_pid]                            # FORK
```

The reference implementation writes 4096 rows per chunk.

## End Marker

8 zero bytes (uncompressed_size = 0, compressed_size = 0).

## Example: Reading in Python

```python
import struct, lz4.block, msgpack

def read_tracegrind(filepath):
    with open(filepath, 'rb') as f:
        assert f.read(4) == b'TGMP'
        version = struct.unpack('<I', f.read(4))[0]
        assert version == 2

        # Read schema chunk
        usize, csize = struct.unpack('<II', f.read(8))
        schema = msgpack.unpackb(
            lz4.block.decompress(f.read(csize), uncompressed_size=usize))
        event_schemas = {
            int(k): [c.decode() if isinstance(c, bytes) else c for c in v]
            for k, v in schema[b'event_schemas'].items()
        }

        # Read data chunks
        rows = []
        while True:
            usize, csize = struct.unpack('<II', f.read(8))
            if usize == 0 and csize == 0:
                break
            chunk = lz4.block.decompress(f.read(csize), uncompressed_size=usize)
            unpacker = msgpack.Unpacker(raw=False)
            unpacker.feed(chunk)
            for row in unpacker:
                event_type = row[2]
                columns = event_schemas[event_type]
                rows.append(dict(zip(columns, row)))

        return event_schemas, rows
```

## References

- [MsgPack Specification](https://github.com/msgpack/msgpack/blob/master/spec.md)
- [LZ4 Block Format](https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md)

## Reference Implementation

- `tracegrind/tg_msgpack.c/h` - MsgPack encoder
- `tracegrind/tg_lz4.c/h` - LZ4 compression wrapper
- `tracegrind/lz4.c/h` - Vendored LZ4 library
- `tracegrind/dump.c` - Trace output integration
