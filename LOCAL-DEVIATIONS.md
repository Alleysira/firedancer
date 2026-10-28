# Local development deviations

This fork is not byte-identical to upstream. Its original ff881b69 base
contains five GCC conversion fixes and a NUMA fallback retained from earlier
component experiments. Native Linux validator runs must use real NUMA sysfs.

## Full-validator sleep-mode PoH (2026-10-28)

Commits aaee882 and a1f6a32 introduced zero-count normalization and allowed
low-power mixins. They removed crashes but consumed tick positions for
transaction entries: Agave/Jito rejected leader blocks with TooFewTicks.
The follow-up keeps timer ticks separate from transaction mixins, reserves
only the last tick until done_packing, and never marks a transaction entry
as the final tick. Numeric hash-count mode keeps its previous behavior.

test_poh_full drives the actual PoH state machine with a controlled clock
and captured publications. It verifies entry hash chains, empty tick counts,
transaction entries at the first/last tick, failed transactions, skipped
slots, numeric configurations, and reset. Live acceptance also requires
Firedancer-produced blocks finalized by Agave/Jito; voting alone is not a
passing result.
