# Windows Pipe Bug CI Test

This workflow tests the Windows `_popen()` zero-byte read bug and verifies the fix.

## Purpose

The workflow runs on Windows and:
1. Confirms the extension builds correctly on Windows
2. Tests basic pipe functionality with PowerShell commands
3. Tests both legacy and fixed pipe close behaviors
4. Demonstrates that the bug can be reproduced with the `use_legacy_pipe_close` flag

## Key Changes

**Important:** This workflow does NOT run the Unix-specific test files (`shellfs.test`, `arrow_pipe.test`, `windows_bug_regression.test`) because they use Unix commands like `seq`, `awk`, and `grep` that don't exist on Windows. Instead, it uses PowerShell-native commands.

## What the Tests Do

### Basic Pipe Test
Tests reading 100 rows of CSV data from PowerShell:
```sql
SELECT COUNT(*) FROM read_csv('powershell -Command "1..100 | ForEach-Object { Write-Output \"$_,$_\" }" |');
```

**Expected Result:** 100 rows successfully read

### Fixed Behavior Test
Tests the fix with 10,000 rows using `use_legacy_pipe_close = false` (default):
```sql
SET use_legacy_pipe_close = false;
SELECT COUNT(*), SUM(column0) FROM read_csv('powershell -Command "1..10000 | ForEach-Object { Write-Output \"$_,$_\" }" |');
```

**Expected Result:** Successfully reads all 10,000 rows

### Legacy Behavior Test (Bug Demonstration)
Tests with legacy behavior using `use_legacy_pipe_close = true`:
```sql
SET use_legacy_pipe_close = true;
SELECT COUNT(*) FROM read_csv('powershell -Command "1..10000 | ForEach-Object { Write-Output \"$_,$_\" }" |');
```

**Expected Result on Windows:**
- May fail with truncated data or incomplete reads (demonstrates the bug)
- Uses `continue-on-error: true` so workflow doesn't fail if this demonstrates the bug

## Why This Confirms the Bug

1. **Platform-Specific**: Runs on actual Windows runners where the bug manifests
2. **Native Commands**: Uses PowerShell which properly exercises `_popen()` on Windows
3. **Moderate Dataset**: Uses 10,000 rows to trigger buffering without excessive CI time
4. **Controlled Comparison**: Tests both behaviors side-by-side with the same data
5. **Automated Verification**: Runs on every push and PR to ensure regression doesn't occur

## Running Locally on Windows

To test locally on Windows:

```powershell
# Build the extension
make release

# Test with fixed behavior (should succeed)
build\release\duckdb.exe -c "LOAD './build/release/extension/shellfs/shellfs.duckdb_extension'; SET use_legacy_pipe_close = false; SELECT COUNT(*) FROM read_csv('powershell -Command \"1..10000 | ForEach-Object { Write-Output \\\"$_,$_\\\" }\" |');"

# Test with legacy behavior (may fail)
build\release\duckdb.exe -c "LOAD './build/release/extension/shellfs/shellfs.duckdb_extension'; SET use_legacy_pipe_close = true; SELECT COUNT(*) FROM read_csv('powershell -Command \"1..10000 | ForEach-Object { Write-Output \\\"$_,$_\\\" }\" |');"
```

## Understanding the Results

### If Legacy Test Fails
This confirms the bug exists on Windows. The failure indicates that `_popen()` returned 0 bytes temporarily during buffering, causing premature pipe closure.

### If Fixed Test Succeeds
This confirms the fix works. The `feof()` check correctly distinguishes between temporary zero-byte reads and actual EOF.

### If Both Succeed
This might happen on:
- Very fast systems where buffering delays are minimal
- Specific Windows configurations
- When system load is low

However, the moderate dataset size (10,000 rows) is designed to increase the likelihood of triggering buffering conditions on most Windows systems.

## CI Workflow Status

The workflow is configured with:
- **Basic test**: Must pass (validates pipe functionality)
- **Fixed behavior test**: Must pass (validates the fix works)
- **Legacy behavior test**: `continue-on-error: true` (expected to potentially fail, demonstrating the bug)

This approach allows us to:
1. Validate the fix works correctly
2. Demonstrate the bug exists when the flag is set to `true`
3. Not block PRs when demonstrating expected failure behavior

## Differences from Unix Tests

The Unix test files (`test/sql/*.test`) use commands like:
- `seq` - Generate sequences
- `awk` - Text processing
- `grep` - Pattern matching

These don't exist on Windows by default. The Windows workflow uses PowerShell equivalents:
- `1..N` - Range operator for sequences
- `ForEach-Object` - Processing pipeline
- Native PowerShell filtering

Both approaches test the same underlying shellfs functionality but with platform-appropriate commands.
