// A stalled CYW43 blocks ~1 s per touch; the guard's job is to keep that off the
// main loop: quarantine on the first stall, one reset attempt per quarantine, and
// re-quarantine when the reset itself stalls.
#include <cassert>
#include "pip/radio_guard.hpp"

using pip::RadioGuard;

static void healthy_radio_stays_allowed() {
    RadioGuard g;
    assert(g.allow(0));
    g.report(1000, 2);            // a normal WL transaction, microseconds rounded up
    assert(g.allow(1001));
    assert(!g.should_reset(1001));
    assert(g.stalls() == 0);
}

static void stall_quarantines_then_offers_one_reset() {
    RadioGuard g;
    g.report(1000, 800);          // one blocked call
    assert(g.stalls() == 1);
    assert(!g.allow(1001));
    assert(!g.allow(1000 + RadioGuard::kQuarantineMs - 1));
    assert(!g.should_reset(30000));                          // not before the quarantine ends
    assert(g.should_reset(1000 + RadioGuard::kQuarantineMs));
    assert(!g.should_reset(1000 + RadioGuard::kQuarantineMs + 1));   // once, not every frame
    assert(g.allow(1000 + RadioGuard::kQuarantineMs + 1));   // reset consumed, radio on probation
}

static void stalled_reset_requarantines() {
    RadioGuard g;
    g.report(1000, 800);
    uint32_t after = 1000 + RadioGuard::kQuarantineMs;
    assert(g.should_reset(after));
    g.report(after, 900);         // the chip power-cycle itself hung
    assert(g.stalls() == 2);
    assert(!g.allow(after + 1));
    assert(g.should_reset(after + RadioGuard::kQuarantineMs));
}

static void recovery_clears_probation() {
    RadioGuard g;
    g.report(1000, 800);
    uint32_t after = 1000 + RadioGuard::kQuarantineMs;
    assert(g.should_reset(after));
    g.report(after, 3);           // reset came back fast: healthy again
    assert(g.allow(after + 1));
    assert(!g.should_reset(after + 2 * RadioGuard::kQuarantineMs));
}

int main() {
    healthy_radio_stays_allowed();
    stall_quarantines_then_offers_one_reset();
    stalled_reset_requarantines();
    recovery_clears_probation();
    return 0;
}
