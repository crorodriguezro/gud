# OnePlus 6 GUD Connector Instrumentation Design

## Purpose

The `connector` diagnostic stage resets the phone. Linux 4.9 handles the
connector query while holding `mode_config.mutex`, invoking GUD detect, GUD
fixed-mode creation, and generic probe-helper processing. No persisted crash
record identifies which interval fails.

## Scope

Add temporary `dev_info()` boundary markers only in `backport-4.9/gud_connector.c`.
No USB request, DRM object setup, mode timing, KMS callback, or locking behavior
may change.

## Markers

`gud_connector_detect()` logs:

1. entry;
2. the status selected under `gud->lock`, immediately before return.

`gud_connector_get_modes()` logs:

1. entry;
2. successful return from `drm_mode_create()`;
3. return from `drm_mode_probed_add()`;
4. immediately before returning mode count `1`.

Use stable `GUD connector:` prefixes so the streamed dmesg can be filtered
without matching unrelated kernel output.

## Validation

Build against the exact target tree, run the existing contract suite, and rerun
only the `connector` stage through the reset-safe runner. The final emitted
marker identifies the last completed driver boundary before reset:

- no detect entry: failure occurs before the GUD detect callback;
- detect entry without status: failure occurs while acquiring or holding the
  GUD mutex;
- detect status without get-modes entry: failure occurs in probe-helper logic
  between callbacks;
- get-modes entry without mode-created: failure occurs in `drm_mode_create()`;
- mode-created without probed-add: failure occurs in fixed mode initialization;
- probed-add without get-modes completion: failure occurs after list insertion
  or while returning to the generic helper;
- all markers before reset: failure occurs in generic probe-helper processing
  after `get_modes()` returns.

## Removal

These markers are diagnostic-only. Remove them when the connector-query root
cause is identified and its permanent fix is validated. They must not remain in
the normal Ticket 4 driver path.

## Non-Goals

- No workaround or behavior change for the connector reset.
- No atomic, GEM, USB transfer, vblank, or display-state modification.
- No Ticket 4 completion claim.
