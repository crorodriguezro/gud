# E2 v1 closure

Status: **E2 COMPLETE FOR V1**  
Date: 2026-08-24  
Next milestone: **E3 — hotplug/reconnect** (not started)

This is the durable cross-repository closure record for the bounded,
asynchronous Lomiri external-display presentation path. It does not claim that
reconnect or automatic reactivation is implemented; those are E3.

## Selected transport

The selected E2 v1 transport candidate is:

```text
Lomiri -> unmodified Mir 1.8.3 -> direct packed RGB565 screencast
  -> LatestFramePresenter -> mirgud -> GUD RGB565 -> LZ4 -> USB
  -> Raspberry Pi Zero 2 W FunctionFS gadget -> VC4 DRM/KMS -> HDMI
```

Mode is 1280x720. A full logical RGB565 frame is 1,843,200 bytes. No
XRGB8888-to-RGB565 conversion is required in `mirgud`.

| Path | Qualified presented rate | Decision |
| --- | ---: | --- |
| XRGB8888 RAW | ~9 FPS | historical baseline |
| XRGB8888 + LZ4 | 13.27 FPS | historical baseline |
| Direct Mir RGB565 RAW | 18.16 FPS | simpler fallback |
| Direct Mir RGB565 + LZ4 | **22.62 FPS** | selected E2 v1 candidate |

The candidate exceeds the >=20 FPS E2 qualification target. It is not called
the installed final production default; E5-T03 retains the final
apples-to-apples release-default and image-quality decision.

## Capability decision

The deployed stack is Mir 1.8.3, Lomiri 0.5.0, Android2/libhybris/gralloc on
the OnePlus 6:

| Format | Mir runtime | Packing | Project status |
| --- | --- | --- | --- |
| ABGR8888 | supported/current under `auto` | 4 Bpp | historical/current source path |
| XRGB8888 | direct request rejected/not advertised | n/a | not a direct Mir source |
| RGB888 | supported | true packed 3 Bpp | available, not selected |
| RGB565 | supported | true packed 2 Bpp | selected direct source |

Canonical audit: `../gud-gadget/evidence/xdisp-mir-format-capability-20260824T003458Z/`.

## Qualification disposition

| Gate | Result | Durable evidence/disposition |
| --- | --- | --- |
| E2-T01 | PASS | existing canonical E2-T01 evidence |
| E2-T02 | PASS | existing worker/component and managed-path evidence |
| E2-T03 | PASS | existing nonblocking/containment evidence |
| E2-T04 | PASS by project-owner decision | `../gud-gadget/evidence/xdisp-e2-t04-rgb565-lz4-soak-20260824T033632Z/`; 30-minute soak, 24.474 FPS, no observed RSS/FD/thread/sync-file leak, zero poisoned/ambiguous I/O |
| E2-T05-A | 10/10 bounded | `../gud-gadget/evidence/xdisp-e2-t05-shutdown-containment-20260824T133342Z/`; accepted forced containment at the Mir disconnect boundary |
| E2-T05-B | 3/3 PASS | GUD absent; bounded/idempotent, no child or respawn storm |
| E2-T05-C | 2/2 PASS | real physical USB DATA detach; phone/compositor/LightDM survived |
| E2-T05-D | 3/3 PASS | safe pre-ownership stall contained |
| E2-T05-E | 3/3 PASS | deterministic pre-bulk EIO before ambiguous bulk ownership |
| E2-T05-F | VERIFIED | known Mir disconnect boundary reproduced naturally |

T05 evidence is preserved in
`../gud-gadget/evidence/xdisp-e2-t05-shutdown-containment-20260824T133342Z/`
and
`../gud-gadget/evidence/xdisp-e2-t05-bce-completion-20260824T135743Z/`.
Both evidence bundles retain their original measurements and checksum
manifests.

## Safety and lifecycle caveat

The E1 invariants remain in force: at most one logical accepted/in-flight GUD
payload, no payload pipelining, bounded retry only for explicit pre-bulk
`SET_BUFFER -EBUSY`, no retry of ambiguous accepted bulk I/O, and ambiguous
accepted I/O transitions to `Poisoned`. No UDC unbind, gadget restart, or
reboot is used as an accepted-I/O recovery shortcut.

The deployed Mir 1.8.3 disconnect path has a known graceful-shutdown caveat:
after presenter/KMS and screencast cleanup, `mir_connection_release` may
stall. xdispd applies its existing bounded containment, force-terminates and
reaps the managed child, and keeps the phone/compositor/LightDM healthy. This
is **bounded containment at the known Mir disconnect boundary**, not a
graceful-shutdown fix. E3 owns reconnect and reappearance behavior.

The qualified FunctionFS receive design remains one logical AIO backed by
vmalloc storage, internally split into sequential 16 KiB DMA-compatible
chunks, with one logical completion. For a 1,843,200-byte RGB565 frame this
is 112 x 16,384-byte chunks plus one 8,192-byte chunk: 113 internal requests,
one logical GUD payload.

## Change audit

The reported “38-file” completion changeset was reconciled against Git. The
latest evidence commit `5145620` contains 37 Git paths (the UI count was an
approximation); the complete unpushed `gud-gadget` series contains 91 unique
paths across four commits. Every one is under one of the two canonical T05
evidence roots and is classified as E2 evidence (category C), not source or
generated garbage. The one unpushed Mir path is the intentional default-off
diagnostic hook (category B).

| Repository | Commit | Audited path coverage | Type | Reason | Keep/publish |
| --- | --- | --- | --- | --- | --- |
| `gud-gadget` | `c5abd03` | all paths under `evidence/xdisp-e2-t05-shutdown-containment-20260824T133342Z/` | C | initial T05-A/D/F evidence and metadata | yes |
| `gud-gadget` | `b53217f` | `.../xdisp-e2-t05-shutdown-containment-20260824T133342Z/{final-report.md,SHA256SUMS}` | C | finalize T05 report metadata and hashes | yes |
| `gud-gadget` | `b57e460` | all paths under `evidence/xdisp-e2-t05-bce-completion-20260824T135743Z/` | C | record B/C/E continuation evidence | yes |
| `gud-gadget` | `5145620` | 37 paths under `.../xdisp-e2-t05-bce-completion-20260824T135743Z/` | C | replace blocked placeholders with measured PASS results and valid checksums | yes |
| `mir-android2-platform-gud` | `0cd8f5b` | `src/utils/gud_screencast.cpp` | B | deterministic pre-bulk EIO hook, environment-gated and default OFF | yes |

The exact manifest is reproducible with:

```text
git -C gud-gadget diff --name-only origin/pixel-format-benchmark...HEAD
git -C mir-android2-platform-gud diff --name-only github/pixel-format-benchmark...HEAD
```

No category-F path was found in the unpushed E2 commits. Untracked files in
the three working trees were pre-existing or user-owned evidence/build
artifacts; they were preserved and excluded from the closure commits.

## Validation and publication gate

- Focused Android2 tests: 6/6 `GudPresentationWorker.*:GudHwcBoundary.*`.
- Focused xdisp unit tests: 50/50.
- Offline `xdisp-p0.2-build.sh` completed successfully.
- T05 evidence `SHA256SUMS` validation: PASS.
- Mir safe-stall and pre-bulk hooks are environment-gated and default OFF;
  normal production configuration cannot trigger them.
- No Mir or Lomiri production behavior, lifecycle timeout, or transport rule
  was changed for closure.

The final publication record is completed by the Git push and remote-state
verification for the intended `pixel-format-benchmark` branches.
