/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Private freestanding embedding of the upstream LZ4 block compressor.
 *
 * Upstream source: https://github.com/lz4/lz4.git
 * Revision: 0774d05537f9762f838f7ab541b7765f1a729cb5
 * Files: vendor/lz4-1.10.0/lz4.c and vendor/lz4-1.10.0/lz4.h
 *
 * This is compiled into gud.ko and exports no LZ4 kernel service.  The
 * wrapper deliberately exposes only the external-state bounded-output call
 * used by the XDISP rectangle planner.
 */

#ifdef __KERNEL__
#include <linux/limits.h>
#include <linux/string.h>
#else
#include <limits.h>
#include <string.h>
#endif

#define LZ4_FREESTANDING 1
#define LZ4_memcpy memcpy
#define LZ4_memmove memmove
#define LZ4_memset memset
#define LZ4_DISABLE_STACK_COMPRESSION 1

/* Keep all upstream API symbols local to this translation unit. */
#define LZ4LIB_VISIBILITY static

/* The one upstream function without LZ4LIB_API gets a GUD-private name. */
#define LZ4_compress_destSize_extState \
	gud_lz4_upstream_compress_dest_size_extstate
#define LZ4_compress_fast_extState \
	gud_lz4_upstream_compress_fast_extstate

#include "vendor/lz4-1.10.0/lz4.c"

#include "gud_xdisp_lz4.h"

size_t gud_xdisp_lz4_upstream_workmem_size(void)
{
	return LZ4_sizeofState();
}

size_t gud_xdisp_lz4_compress_dest_size(const u8 *source,
					 size_t *source_length,
					 u8 *destination,
					 size_t destination_capacity,
					 void *workmem)
{
	int source_size;
	int result;

	if (!source || !source_length || !*source_length || !destination ||
	    !destination_capacity || !workmem ||
	    *source_length > ((size_t)~0U >> 1) ||
	    destination_capacity > ((size_t)~0U >> 1))
		return 0;

	source_size = *source_length;
	result = gud_lz4_upstream_compress_dest_size_extstate(
		workmem, (const char *)source, (char *)destination,
		&source_size, (int)destination_capacity, 1);
	if (result <= 0 || source_size <= 0)
		return 0;

	*source_length = source_size;
	return result;
}

size_t gud_xdisp_lz4_compress_limited(const u8 *source,
				      size_t source_length, u8 *destination,
				      size_t destination_capacity, void *workmem)
{
	int result;

	if (!source || !source_length || !destination || !destination_capacity ||
	    !workmem || source_length > ((size_t)~0U >> 1) ||
	    destination_capacity > ((size_t)~0U >> 1))
		return 0;

	/*
	 * This is limited-output, not prefix discovery: a zero result means the
	 * whole candidate did not fit. The caller must fall back rather than send
	 * any partial result.
	 */
	result = gud_lz4_upstream_compress_fast_extstate(
		workmem, (const char *)source, (char *)destination,
		(int)source_length, (int)destination_capacity, 1);
	if (result <= 0)
		return 0;

	return result;
}
