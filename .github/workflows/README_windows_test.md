# Windows Pipe Bug CI Test

This workflow tests the Windows `_popen()` zero-byte read bug and verifies the fix.

## Purpose

The workflow runs on Windows and:
1. Confirms the extension builds correctly on Windows
2. Runs all standard tests to ensure basic functionality
3. Tests both legacy and fixed pipe close behaviors
4. Demonstrates that the bug can be reproduced with the `use_legacy_pipe_close` flag

## What the Tests Do

### Standard Tests
- `shellfs.test` - Basic pipe functionality
- `arrow_pipe.test` - Multi-buffer pipe reads with large datasets
- `windows_bug_regression.test` - Tests both legacy and fixed behaviors

### Bug Demonstration Tests

#### Test with Legacy Behavior (Bug Reproduction)
```sql
SET use_legacy_pipe_close = true;
SELECT COUNT(*) FROM read_csv('powershell -Command "1..50000 | ForEach-Object { Write-Output \"$_,$_\" }" |');
```

This test uses PowerShell to generate 50,000 rows of CSV data. With `use_legacy_pipe_close = true`, the extension uses the old buggy behavior where pipes close on zero-byte reads without checking `feof()`.

**Expected Result on Windows:**
- May fail with truncated data or incomplete reads
- Demonstrates the bug under Windows buffering conditions

#### Test with Fixed Behavior (Bug Fix Verification)
```sql
SET use_legacy_pipe_close = false;  -- This is the default
SELECT COUNT(*) FROM read_csv('powershell -Command "1..50000 | ForEach-Object { Write-Output \"$_,$_\" }" |');
```

This test uses the same PowerShell command but with the fix enabled (default behavior).

**Expected Result:**
- Should always succeed and return 50,000 rows
- Proves the fix works correctly

## Why This Confirms the Bug

1. **Platform-Specific**: Runs on actual Windows runners where the bug manifests
2. **Large Dataset**: Uses 50,000 rows to increase likelihood of triggering buffering delays
3. **Controlled Comparison**: Tests both behaviors side-by-side with the same data
4. **Automated Verification**: Runs on every push and PR to ensure regression doesn't occur

## Running Locally on Windows

To test locally on Windows:

```powershell
# Build the extension
make release

# Test with fixed behavior (should succeed)
build\release\duckdb.exe -c "LOAD './build/release/extension/shellfs/shellfs.duckdb_extension'; SET use_legacy_pipe_close = false; SELECT COUNT(*) FROM read_csv('powershell -Command \"1..50000 | ForEach-Object { Write-Output \\\"$_,$_\\\" }\" |');"

# Test with legacy behavior (may fail)
build\release\duckdb.exe -c "LOAD './build/release/extension/shellfs/shellfs.duckdb_extension'; SET use_legacy_pipe_close = true; SELECT COUNT(*) FROM read_csv('powershell -Command \"1..50000 | ForEach-Object { Write-Output \\\"$_,$_\\\" }\" |');"
```

## Understanding the Results

### If Legacy Test Fails
This confirms the bug exists on Windows. The failure indicates that `_popen()` returned 0 bytes temporarily during buffering, causing premature pipe closure.

### If Fixed Test Succeeds
This confirms the fix works. The `feof()` check correctly distinguishes between temporary zero-byte reads and actual EOF.

### If Both Succeed
This might happen on:
- Very fast systems where buffering delays are minimal
- Small datasets that don't trigger the buffering condition
- Unix/Linux where the buffering behavior is different

However, the tests are designed with large datasets (50,000 rows) to maximize the chance of triggering the bug on Windows.

## CI Workflow Status

The workflow is configured with `continue-on-error: true` for the legacy behavior test, meaning:
- The overall workflow succeeds even if the legacy test fails
- The failure is informational, showing that the bug can be reproduced
- The important part is that the fixed behavior test succeeds

This approach allows us to:
1. Demonstrate the bug exists (legacy test may fail)
2. Verify the fix works (fixed test should succeed)
3. Not block PRs when demonstrating expected failure behavior
