/*
 * qoir_codec.h - accessors for the RGB565<->RGB888 conversion vs QOIR
 * library encode/decode timing breakdown (PROJECT SPEC next-phase Phase
 * 3.D / Phase 7: "Do not hide RGB565 conversion inside QOIR timing").
 *
 * These report the *most recent* qoir-lossless/qoir-lossy-l3/
 * qoir-lossy-l5 encode()/decode() call's breakdown. The benchmark harness
 * is single-threaded and calls these immediately after the corresponding
 * fbcodec_desc encode()/decode() call, so there is no reentrancy concern.
 */
#ifndef FBCODEC_QOIR_CODEC_H
#define FBCODEC_QOIR_CODEC_H

#ifdef __cplusplus
extern "C" {
#endif

void qoir_last_encode_breakdown_ns(double *conversion_ns, double *codec_ns);
void qoir_last_decode_breakdown_ns(double *conversion_ns, double *codec_ns);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_QOIR_CODEC_H */
