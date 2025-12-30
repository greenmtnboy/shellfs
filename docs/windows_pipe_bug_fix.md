# Windows _popen() Premature Pipe Closure Bug - Replication and Fix

## Bug Description

The shellfs DuckDB extension had a critical bug on Windows where the `ShellFileSystem::Read()` method would close pipes immediately upon receiving a zero-byte read from `_popen()`. This caused truncation of data streams, particularly Arrow IPC streams, resulting in errors like:

```
arrow_scan: get_next failed(): {"exception_type":"Serialization","exception_message":"not enough data in file to deserialize result"}
```

## Root Cause

On Windows, `_popen()` combined with `fread()` can temporarily return 0 bytes when:
1. The child process is still writing data
2. Data is being buffered
3. EOF has **not** been reached yet

This behavior differs from Unix/Linux where a 0-byte read from `fread()` typically indicates EOF. The original code assumed 0 bytes always meant EOF:

```cpp
int64_t bytes_read = fread(buffer, 1, nr_bytes, pipe);
if (bytes_read == 0) {
    handle.Close();  // INCORRECT: Closes even when more data is coming!
}
```

## The Fix

The fix uses `feof()` to distinguish between true EOF and temporary empty buffers:

```cpp
int64_t bytes_read = fread(buffer, 1, nr_bytes, pipe);
if (bytes_read == 0 && feof(pipe)) {
    // Only close when we've actually reached EOF
    handle.Close();
}
```

This ensures the pipe stays open when buffers are temporarily empty but data is still being produced.

## Bug Replication

### Prerequisites
- Windows OS (where `_popen()` exhibits the zero-byte read behavior)
- DuckDB with shellfs extension (pre-fix version)
- Arrow extension or any tool that produces binary streams

### Replication Steps

1. **Install affected version of shellfs extension:**
```sql
LOAD shellfs;
```

2. **Attempt to read Arrow IPC stream through a pipe:**
```sql
-- This would fail on Windows with the bug
COPY (SELECT i FROM range(10000) t(i)) TO 'data.arrow' (FORMAT ARROW);
SELECT COUNT(*) FROM read_parquet('cat data.arrow |', format='arrow');
```

3. **Expected behavior (with bug):**
   - Query fails with serialization error
   - Error message: "not enough data in file to deserialize result"
   - Pipe closes prematurely during read

4. **Expected behavior (with fix):**
   - Query succeeds
   - All 10,000 rows are read correctly
   - Pipe stays open until actual EOF

## Test Coverage

The fix includes comprehensive test coverage in `test/sql/arrow_pipe.test`:

1. **Large CSV data stream** - Tests multiple buffer reads (10,000 rows)
2. **Delayed data generation** - Simulates buffering delays with sleep
3. **Very large dataset** - Ensures robustness with 50,000 rows

All tests pass on both Unix/Linux and Windows platforms.

## Technical Details

### File Modified
- `src/shell_file_system.cpp` - Lines 231-244 in `ShellFileSystem::Read()`

### Version Updated
- Extension version bumped to `2025123001` (format: YYYYMMDDCC)

### Why `feof()` Works

The `feof()` function checks the EOF indicator on the file stream:
- Returns `true` only when the stream has actually reached its end
- Returns `false` when buffers are temporarily empty but stream is still open
- Works consistently across Windows and Unix/Linux platforms

### Platform Differences

| Platform | `fread()` returns 0 bytes | Typical Cause |
|----------|---------------------------|---------------|
| Unix/Linux | Usually indicates EOF | End of stream reached |
| Windows | May be temporary | Buffers not ready, process still writing |

Using `feof()` makes the behavior consistent across platforms.

## Impact

This fix resolves:
- Arrow IPC stream truncation on Windows
- Premature pipe closure during large data transfers
- Binary data format corruption issues
- General reliability of pipe-based I/O on Windows

## Backwards Compatibility

The fix maintains full backwards compatibility:
- No API changes
- No breaking changes to existing functionality
- All existing tests continue to pass
- Fix is transparent to users
