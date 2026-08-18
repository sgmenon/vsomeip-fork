# Shared TSAN_OPTIONS for in-process tests that start a real routing manager.
# libtsan + gcc11/libstdc++ mis-tracks condition_variable::wait_for
# (https://github.com/google/sanitizers/issues/1259), which shows up as a
# false "double lock" on state_condition_mutex_ in routing_manager_client.
#
# detect_deadlocks=0: the same wait_for bug leaks lock-tracking slots, so
# tests that construct many applications (client_id exhaust-range) abort
# with n_all_locks_ == 64 in sanitizer_deadlock_detector.h.
#
# Pair with: data = [TSAN_SUPPRESSIONS]

TSAN_SUPPRESSIONS = "//test/network_tests:tsan_suppressions"

TSAN_OPTIONS = (
    "verbosity=3:halt_on_error=1:detect_deadlocks=0:" +
    "suppressions=$(rootpath " + TSAN_SUPPRESSIONS + ")"
)
