#ifndef FBCODEC_CSV_OUT_H
#define FBCODEC_CSV_OUT_H

#include "result.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Appends one row per result; writes the header first if the file is
 * being created (does not already exist / is empty).
 */
bool csv_append_results(const char *path, const bench_result *results,
			 size_t count);

#ifdef __cplusplus
}
#endif

#endif /* FBCODEC_CSV_OUT_H */
