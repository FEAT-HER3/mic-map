# Phase 9 Deferred Items

Pre-existing issues discovered during Phase 9 execution that are out of scope
for the current plan and should be addressed in a follow-up.

## PutSettingsRoundTrip ctest failing (exit 3)

**Discovered during:** 09-02 Task 4 (driver_api extension) regression check.

**Status:** Pre-existing — fails identically on HEAD (commit 5edc48b) BEFORE
any 09-02 Task 4 edits, and reproduces with the test executable run directly.

**Symptom:** `tests/driver/put_settings_round_trip_test.cpp` exits with code 3
(C++ exception not caught by `MM_CHECK`). Process logs show the HttpServer
starts, accepts a request, then stops cleanly — but exit code 3 indicates a
caught-by-runtime exception (likely `nlohmann::json::exception` thrown by
`nlohmann::json::parse(ss.str())` against the on-disk file when the
disk-write content has not yet flushed).

**Out of scope for 09-02:** No 09-02 file (settings_validator, http_server,
device_provider, driver_api) is modified by this test path. Test exercises
PUT /settings → saveConfigJson → on-disk readback. Plan 09-02 ships only the
training endpoints + driver_api training methods.

**Suggested fix path:** Likely needs an explicit `f.close()` between the
saveConfigJson and the readback `std::ifstream` open, or a brief sleep, or a
`std::filesystem::file_size` poll loop to let the OS flush the FILE_SHARE_*
handle. Belongs in P8 follow-up (08-04 or a P10 housekeeping plan).
