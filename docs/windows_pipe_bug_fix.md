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

## Testing the Bug with the `use_legacy_pipe_close` Flag

To facilitate testing and bug confirmation, the extension now includes a `use_legacy_pipe_close` configuration option:

```sql
-- Enable legacy behavior (reproduces the bug)
SET use_legacy_pipe_close = true;

-- Disable legacy behavior (uses the fix) - THIS IS THE DEFAULT
SET use_legacy_pipe_close = false;
```

### Purpose of the Flag

The `use_legacy_pipe_close` flag allows you to:
1. **Confirm the bug exists on Windows** by setting it to `true`
2. **Verify the fix works** by comparing behavior with it set to `false` (default)
3. **Run regression tests** to ensure the bug doesn't reoccur

### Important Notes

- **Default behavior is FIXED**: `use_legacy_pipe_close` defaults to `false`, meaning the fix is active
- **Legacy mode is for testing only**: Setting this to `true` intentionally reproduces the bug
- **Platform differences**: The bug is most apparent on Windows with large datasets or under system load

## Bug Replication

### Prerequisites
- Windows OS (where `_popen()` exhibits the zero-byte read behavior)
- DuckDB with shellfs extension
- Large dataset that requires multiple buffer reads

### Replication Steps

1. **Install shellfs extension:**
```sql
LOAD shellfs;
```

2. **Test with legacy behavior (should fail on Windows with large datasets):**
```sql
SET use_legacy_pipe_close = true;

-- This may fail on Windows with large datasets due to premature pipe closure
SELECT COUNT(*) FROM read_csv('powershell -Command "1..50000 | ForEach-Object { Write-Output \"$_,$_\" }" |');
```

3. **Test with fixed behavior (should succeed):**
```sql
SET use_legacy_pipe_close = false;  -- This is the default

-- This should succeed even with large datasets
SELECT COUNT(*) FROM read_csv('powershell -Command "1..50000 | ForEach-Object { Write-Output \"$_,$_\" }" |');
```

### Expected Results

| Configuration | Platform | Large Dataset | Result |
|---------------|----------|---------------|--------|
| `use_legacy_pipe_close = true` | Windows | Yes | **May fail** with truncated data |
| `use_legacy_pipe_close = true` | Unix/Linux | Yes | Usually succeeds (different behavior) |
| `use_legacy_pipe_close = false` | Windows | Yes | **Succeeds** |
| `use_legacy_pipe_close = false` | Unix/Linux | Yes | **Succeeds** |

## Automated Testing

### CI Workflow

A dedicated Windows CI workflow (`.github/workflows/windows_pipe_test.yml`) tests both behaviors:

1. Runs all standard tests with the fix enabled (default)
2. Tests legacy behavior with large datasets (expected to potentially fail)
3. Tests fixed behavior with large datasets (expected to succeed)

This provides continuous verification that:
- The fix works correctly
- The bug is reproducible when the flag is set to `true`
- The default behavior is correct

## Test Coverage

The fix includes comprehensive test coverage:

1. **test/sql/arrow_pipe.test** - Multi-buffer pipe reads (10,000-50,000 rows)
2. **test/sql/windows_bug_regression.test** - Tests both legacy and fixed behavior with the flag
3. **Windows CI workflow** - Automated testing on actual Windows runners

All tests pass on both Unix/Linux and Windows platforms.

## Technical Details

### File Modified
- `src/shell_file_system.cpp` - Lines 222-260 in `ShellFileSystem::Read()`
- `src/shellfs_extension.cpp` - Added `use_legacy_pipe_close` config option

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
- New `use_legacy_pipe_close` flag defaults to `false` (fixed behavior)
