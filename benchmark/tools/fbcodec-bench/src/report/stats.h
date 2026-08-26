/*
 * stats.h - percentile aggregation over raw nanosecond timing samples
 * (PROJECT SPEC section 18: mean/median/p90/p95/p99/min/max, never just an
 * average).
 */
#ifndef FBCODEC_STATS_H
#define FBCODEC_STATS_H

#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	double mean;
	double median;
	double p90;
	double p95;
	double p99;
	double min;
	double max;
} percentile_stats;

/* Sorts `samples` in place (ascending) and computes percentile_stats. */
percentile_stats stats_compute(uint64_t *samples, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_STATS_H */
