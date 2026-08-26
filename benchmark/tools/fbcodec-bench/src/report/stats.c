#include <stdlib.h>

#include "stats.h"

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a;
	uint64_t y = *(const uint64_t *)b;

	if (x < y)
		return -1;
	if (x > y)
		return 1;
	return 0;
}

static double percentile_of_sorted(const uint64_t *sorted, size_t n,
				    double p)
{
	double rank;
	size_t lo;
	double frac;

	if (n == 0)
		return 0.0;
	if (n == 1)
		return (double)sorted[0];
	rank = p * (double)(n - 1);
	lo = (size_t)rank;
	frac = rank - (double)lo;
	if (lo + 1 >= n)
		return (double)sorted[n - 1];
	return (double)sorted[lo] +
	       frac * ((double)sorted[lo + 1] - (double)sorted[lo]);
}

percentile_stats stats_compute(uint64_t *samples, size_t n)
{
	percentile_stats s;
	size_t i;
	double sum = 0.0;

	s.mean = s.median = s.p90 = s.p95 = s.p99 = s.min = s.max = 0.0;
	if (n == 0)
		return s;

	qsort(samples, n, sizeof(uint64_t), cmp_u64);
	for (i = 0; i < n; i++)
		sum += (double)samples[i];

	s.mean = sum / (double)n;
	s.median = percentile_of_sorted(samples, n, 0.50);
	s.p90 = percentile_of_sorted(samples, n, 0.90);
	s.p95 = percentile_of_sorted(samples, n, 0.95);
	s.p99 = percentile_of_sorted(samples, n, 0.99);
	s.min = (double)samples[0];
	s.max = (double)samples[n - 1];
	return s;
}
