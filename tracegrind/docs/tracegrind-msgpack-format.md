# Tracegrind MsgPack+LZ4 Output Format

## Overview

Tracegrind's `--output-format=msgpack` produces a binary trace file combining MsgPack serialization with LZ4 block compression. Files use the `.msgpack.lz4` extension.

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
| 4      | 4    | version | Format version, uint32 LE (currently 1) |

## Chunk Format

Each chunk (schema and data) has the same header:

| Offset | Size | Field             | Description |
|--------|------|-------------------|-------------|
| 0      | 4    | uncompressed_size | Size after decompression, uint32 LE |
| 4      | 4    | compressed_size   | Size of LZ4 block, uint32 LE |
| 8      | N    | data              | LZ4 block-compressed MsgPack data |

## Schema Chunk

The first chunk contains a MsgPack map:

```json
{
    "version": 1,
    "format": "tracegrind-msgpack",
    "columns": ["seq", "tid", "event", "fn", "obj", "file", "line", "Ir", ...]
}
```

### Fixed Columns

| Index | Name  | Type   | Description |
|-------|-------|--------|-------------|
| 0     | seq   | uint64 | Sequence number |
| 1     | tid   | int32  | Thread ID |
| 2     | event | int    | 0 = ENTER, 1 = EXIT |
| 3     | fn    | string | Function name |
| 4     | obj   | string | Shared object path |
| 5     | file  | string | Source file path |
| 6     | line  | int32  | Line number (0 if unknown) |

### Event Columns (index 7+)

Event counters as delta values: `Ir`, `Dr`, `Dw`, `I1mr`, `D1mr`, `D1mw`, `ILmr`, `DLmr`, `DLmw`, `Bc`, `Bcm`, `Bi`, `Bim`. Which columns are present depends on Tracegrind options.

## Data Chunks

Each data chunk contains concatenated MsgPack arrays (one per row):

```
[seq, tid, event, fn, obj, file, line, delta_Ir, ...]
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

        # Read schema chunk
        usize, csize = struct.unpack('<II', f.read(8))
        schema = msgpack.unpackb(
            lz4.block.decompress(f.read(csize), uncompressed_size=usize))
        columns = [c.decode() if isinstance(c, bytes) else c
                   for c in schema[b'columns']]

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
                rows.append(dict(zip(columns, row)))

        return columns, rows
```

## References

- [MsgPack Specification](https://github.com/msgpack/msgpack/blob/master/spec.md)
- [LZ4 Block Format](https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md)

## Reference Implementation

- `tracegrind/tg_msgpack.c/h` - MsgPack encoder
- `tracegrind/tg_lz4.c/h` - LZ4 compression wrapper
- `tracegrind/lz4.c/h` - Vendored LZ4 library
- `tracegrind/dump.c` - Trace output integration
