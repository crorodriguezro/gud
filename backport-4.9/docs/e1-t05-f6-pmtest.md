# E1-T05 F6 Runtime PM Test

`xdisp-lz4-12800-pmtest` is a test-only host module variant. It is built with
`GUD_XDISP_PM_TEST`; the production `xdisp-lz4-12800` variant does not enable
that define and retains no USB PM callbacks.

Build the variant:

```sh
make -C backport-4.9 xdisp-lz4-12800-pmtest
modinfo backport-4.9/variants/xdisp-lz4-12800-pmtest/gud.ko
```

The module reports driver name `gud_xdisp_lz4_12800_pmtest`, version
`xdisp-p0.1-adaptive-12800-pmtest-v1`, and a `TEST-ONLY` description. Never
deploy it as the production artifact.

## Runtime PM Procedure

1. Run the mandatory host-mode and VID/PID gate. Discover the actual USB device
   directory by `1d50:614d`; do not assume a fixed topology path.
2. Load the PM-test module and complete one normal exact 12,800-byte probe.
   Preserve the host and Pi Idle markers before changing PM state.
3. Set the device `power/control` to `auto`, set a short
   `power/autosuspend_delay_ms`, and wait for `GUD_PM_TEST suspend result=0`.
   A `result=-EBUSY active_transfer=1` is a valid refusal, not a reason to
   cancel an URB.
4. Confirm the Pi recorded FunctionFS `Suspend` from proven Idle. A host callback
   alone does not qualify F6.
5. Restore `power/control` to `on` or trigger an ordinary supported runtime-PM
   resume. Confirm `GUD_PM_TEST resume result=0` and Pi FunctionFS `Resume`.
   `reset_resume` or a disconnect/reprobe does not qualify F6.
6. Verify the same Pi boot ID, PID, UDC binding, FunctionFS mount, and
   `NRestarts=0`; then run one exact 12,800-byte transfer and prove
   `Processing -> Idle` with no poison or containment.

If the PM-test driver's suspend callback is not reached, or the USB device
cannot enter runtime suspend, retain the PM sysfs state and relevant phone/Pi
logs. Do not modify `a600000.ssusb` for this test.

## Target Result

On the OnePlus 6 target runtime-PM path, the test variant produced real Pi
FunctionFS Suspend and Resume from proven Idle. It then re-enumerated the USB
device instead of calling the persistent driver resume path: topology `1-1.2`
and bus `1` remained, but device number changed `5 -> 6`; Pi observed
`Suspend -> Resume -> Suspend -> Disable -> Enable` and a new activation.
The PM-test `resume` and `reset_resume` markers were absent while a fresh probe
marker was present. This is deferred P2 and does not qualify F6 for v1.
