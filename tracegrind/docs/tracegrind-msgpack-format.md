# Tracegrind MsgPack+LZ4 Output Format

## Overview

Tracegrind produces a binary trace file combining MsgPack serialization with LZ4 block compression. The default output file name is `tracegrind.out.<pid>.msgpack.lz4`.

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
| 4      | 4    | version | Format version, uint32 LE (currently 4) |

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
    "version": 4,
    "format": "tracegrind-msgpack",
    "creator": "valgrind-tracegrind",
    "creator_version": "3.26.0.codspeed",
    "event_schemas": {
        "0": ["seq", "tid", "event", "marker"],
        "1": ["seq", "tid", "event", "fn", "obj", "file", "line", "counters"],
        "2": ["seq", "tid", "event", "fn", "obj", "file", "line", "counters"],
        "3": ["seq", "tid", "event", "fn", "obj", "file", "line", "counters"],
        "4": ["seq", "tid", "event", "fn", "obj", "file", "line", "counters"],
        "5": ["seq", "tid", "event", "child_pid"],
        "6": ["seq", "tid", "event", "child_tid"]
    },
    "counters": ["Ir"],
    "counter_units": {
        "sysTime": "ns",
        "sysCpuTime": "ns"
    }
}
```

The `counters` array lists the dynamic counter column names (e.g. `["Ir"]` or `["Ir", "sysCount", "sysTime", "sysCpuTime"]`). Event schemas for types 1-4 use `"counters"` as a sentinel at index 7 to indicate that a sub-array of counter deltas appears at that position in data rows.

### Event Types

| Type | Name   | Description |
|------|--------|-------------|
| 0    | MARKER | Named marker |
| 1    | ENTER_FN  | Function entry |
| 2    | EXIT_FN | Function exit |
| 3    | ENTER_INLINED_FN | Inlined function entry |
| 4    | EXIT_INLINED_FN  | Inlined function exit |
| 5    | FORK   | Child process created |
| 6    | THREAD_CREATE | New thread created |

### Row Schemas

**MARKER rows (event 0):**

| Index | Name   | Type   | Description |
|-------|--------|--------|-------------|
| 0     | seq    | uint64 | Sequence number |
| 1     | tid    | int32  | Thread ID |
| 2     | event  | int    | 0 = MARKER |
| 3     | marker | string | Marker label |

**ENTER_FN/EXIT_FN rows (event 1, 2):**

| Index | Name     | Type   | Description |
|-------|----------|--------|-------------|
| 0     | seq      | uint64 | Sequence number |
| 1     | tid      | int32  | Thread ID |
| 2     | event    | int    | 1 = ENTER_FN, 2 = EXIT_FN |
| 3     | fn       | string | Function name |
| 4     | obj      | string | Shared object path |
| 5     | file     | string | Source file path |
| 6     | line     | int32  | Line number (0 if unknown) |
| 7     | counters | array  | Counter deltas sub-array (Ir, Dr, Dw, etc.) |

**ENTER_INLINED_FN/EXIT_INLINED_FN rows (event 3, 4):**

Same schema as ENTER_FN/EXIT_FN rows.

| Index | Name     | Type   | Description |
|-------|----------|--------|-------------|
| 0     | seq      | uint64 | Sequence number |
| 1     | tid      | int32  | Thread ID |
| 2     | event    | int    | 3 = ENTER_INLINED_FN, 4 = EXIT_INLINED_FN |
| 3     | fn       | string | Function name |
| 4     | obj      | string | Shared object path |
| 5     | file     | string | Source file path |
| 6     | line     | int32  | Line number (0 if unknown) |
| 7     | counters | array  | Counter deltas sub-array (Ir, Dr, Dw, etc.) |

**FORK rows (event 5):**

| Index | Name      | Type   | Description |
|-------|-----------|--------|-------------|
| 0     | seq       | uint64 | Sequence number |
| 1     | tid       | int32  | Thread ID that called fork |
| 2     | event     | int    | 5 = FORK |
| 3     | child_pid | int32  | PID of the new child process |

**THREAD_CREATE rows (event 6):**

| Index | Name      | Type   | Description |
|-------|-----------|--------|-------------|
| 0     | seq       | uint64 | Sequence number |
| 1     | tid       | int32  | Thread ID that created the new thread |
| 2     | event     | int    | 6 = THREAD_CREATE |
| 3     | child_tid | int32  | Thread ID of the new child thread |

### Event Counter Columns

For ENTER_FN/EXIT_FN/ENTER_INLINED_FN/EXIT_INLINED_FN rows, event counters appear as a sub-array at index 7. The order of values in the sub-array corresponds to the top-level `counters` array in the schema. Which counters are present depends on Tracegrind options:

`Ir`, `Dr`, `Dw`, `I1mr`, `D1mr`, `D1mw`, `ILmr`, `DLmr`, `DLmw`, `Bc`, `Bcm`, `Bi`, `Bim`

### Counter Units

The `counter_units` field is a map from event counter name to its unit string. Only time-based counters are listed; counters absent from the map are dimensionless.

| `--collect-systime` | Entries in `counter_units` |
|---------------------|--------------------|
| `msec`              | `"sysTime": "ms"` |
| `usec`              | `"sysTime": "us"` |
| `nsec`              | `"sysTime": "ns"`, `"sysCpuTime": "ns"` |

When `--collect-systime` is not set, the `counter_units` map is empty.

## Data Chunks

Each data chunk contains concatenated MsgPack arrays. The row format depends on the event type (index 2):

```
[seq, tid, 0, marker]                                    # MARKER
[seq, tid, 1, fn, obj, file, line, [delta_Ir, ...]]      # ENTER_FN
[seq, tid, 2, fn, obj, file, line, [delta_Ir, ...]]      # EXIT_FN
[seq, tid, 3, fn, obj, file, line, [delta_Ir, ...]]      # ENTER_INLINED_FN
[seq, tid, 4, fn, obj, file, line, [delta_Ir, ...]]      # EXIT_INLINED_FN
[seq, tid, 5, child_pid]                                  # FORK
[seq, tid, 6, child_tid]                                  # THREAD_CREATE
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
        assert version == 4

        # Read schema chunk
        usize, csize = struct.unpack('<II', f.read(8))
        schema = msgpack.unpackb(
            lz4.block.decompress(f.read(csize), uncompressed_size=usize),
            raw=False)
        event_schemas = schema['event_schemas']
        counter_names = schema['counters']

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
                columns = event_schemas[str(event_type)]
                record = {}
                for i, col in enumerate(columns):
                    if col == 'counters' and isinstance(row[i], list):
                        # Expand counters sub-array
                        for j, name in enumerate(counter_names):
                            record[name] = row[i][j]
                    else:
                        record[col] = row[i]
                rows.append(record)

        return event_schemas, counter_names, rows
```

## References

- [MsgPack Specification](https://github.com/msgpack/msgpack/blob/master/spec.md)
- [LZ4 Block Format](https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md)

## Reference Implementation

- `tracegrind/tg_msgpack.c/h` - MsgPack encoder
- `tracegrind/tg_lz4.c/h` - LZ4 compression wrapper
- `tracegrind/lz4.c/h` - Vendored LZ4 library
- `tracegrind/dump.c` - Trace output integration
