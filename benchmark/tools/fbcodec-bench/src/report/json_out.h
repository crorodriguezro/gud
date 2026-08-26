#ifndef FBCODEC_JSON_OUT_H
#define FBCODEC_JSON_OUT_H

#include "result.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Writes a JSON array of result objects (with full percentile detail) to
 * `path`. Overwrites any existing file (each invocation of fbcodec-bench
 * produces one JSON document per run; the report stage aggregates many of
 * these files -- see scripts/run_benchmark.sh and scripts/analyze.py).
 */
bool json_write_results(const char *path, const bench_result *results,
			 size_t count, const char *run_metadata_json);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_JSON_OUT_H */
