# Integrated transport disposition

See `summary-final-codecs.md` ("Completion criteria disposition") and
`docs/codec-transport-architecture.md` section 4 for the full detail.

## LZ4 (already integrated, real, running)

The RGB565 + LZ4 path is **not a new integration built for this
benchmark** -- it is the project's existing running production-track
configuration on this rig:

- Kernel side: `gud.ko`'s `GUD_COMPRESSION_LZ4` protocol field
  (`backport-4.9/gud_protocol.h`), a first-class (non-experimental)
  field.
- Pi side: `gud-userspace.service` running
  `/home/cristian/gud-drm-e3-b01-functionfs-epoch` with
  `GUD_TRANSFER_FORMAT=rgb565`, `GUD_TEST_COMPRESSION=lz4`,
  `GUD_TEST_MAX_BUFFER_SIZE=1843200` (verified via `systemctl cat` /
  `journalctl`, see `environment/pi-zero2w.json`).

No new files are produced under `integrated/lz4/`; the isolated
per-stage measurements in `../isolated/op6/*.sender.json` (encode) and
`../isolated/pi/lz4-decode/*.json` (decode) already exercise the
byte-identical vendored LZ4 1.10.0 codec this production path uses.

## JPEG (not integrated this session)

A live, protocol-level JPEG transport integration (new/extended
`compression` codec ID recognized by both `gud.ko` and the Pi's
`gud-drm` userspace) was **not attempted**, for the safety/time reasons
stated in `summary-final-codecs.md`. Every lower-level measurement
needed to evaluate this candidate (OP6 encode cost, Pi hardware decode
latency/throughput/reliability, real interop between an actual
OP6-encoded JPEG and the Pi's hardware decoder, and quality of the
result) was completed and is available under `../isolated/op6/`,
`../isolated/pi/jpeg-hw-decode/`, and `../quality/`.

## R4G4B4 / RGB332 (not integrated; correctly not attempted)

Per PROJECT SPEC section 16 ("do not integrate obviously dominated
candidates"): R4G4B4+LZ4 was measured to be dominated by plain
RGB565+LZ4 (slower encode, only modestly smaller, lossy) and was
therefore not carried into a protocol integration. RGB332+LZ4 is
recommended as a real, evidence-based low-bandwidth *mode*
recommendation (see `summary-final-codecs.md`), but implementing it as
a selectable protocol codec ID was not attempted this session for the
same integration-risk/time reasons as JPEG.
