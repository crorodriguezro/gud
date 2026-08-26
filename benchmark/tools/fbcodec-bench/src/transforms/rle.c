#include "rle.h"

size_t rle_bound(size_t input_len)
{
	/* Worst case: every byte becomes its own 1-byte literal group,
	 * grouped up to 128 bytes per control byte -> overhead <= len/1 + 2.
	 * A generous 2x + small constant keeps this simple and always safe.
	 */
	return input_len * 2 + 64;
}

size_t rle_encode(const uint8_t *src, size_t src_len, uint8_t *dst,
		   size_t dst_cap)
{
	size_t i = 0;
	size_t out = 0;

	while (i < src_len) {
		size_t run = 1;

		while (i + run < src_len && src[i + run] == src[i] &&
		       run < 128)
			run++;

		if (run >= 2) {
			if (out + 2 > dst_cap)
				return (size_t)-1;
			dst[out++] = (uint8_t)(257 - run); /* in [129,255] */
			dst[out++] = src[i];
			i += run;
		} else {
			/* Accumulate a literal run (cap 128 bytes). */
			size_t lit_start = i;
			size_t lit_len = 1;

			i++;
			while (i < src_len && lit_len < 128) {
				/* Stop the literal run if a repeat of >=2
				 * begins here (let the run-branch handle it).
				 */
				if (i + 1 < src_len && src[i] == src[i + 1])
					break;
				if (i + 1 == src_len) {
					lit_len++;
					i++;
					break;
				}
				lit_len++;
				i++;
			}
			if (out + 1 + lit_len > dst_cap)
				return (size_t)-1;
			dst[out++] = (uint8_t)(lit_len - 1); /* in [0,127] */
			{
				size_t k;

				for (k = 0; k < lit_len; k++)
					dst[out++] = src[lit_start + k];
			}
		}
	}
	return out;
}

int rle_decode(const uint8_t *src, size_t src_len, uint8_t *dst,
		size_t dst_cap, size_t *out_len)
{
	size_t i = 0;
	size_t out = 0;

	while (i < src_len) {
		uint8_t c = src[i++];

		if (c <= 127) {
			size_t len = (size_t)c + 1;

			if (i + len > src_len || out + len > dst_cap)
				return -1;
			{
				size_t k;

				for (k = 0; k < len; k++)
					dst[out++] = src[i + k];
			}
			i += len;
		} else if (c >= 129) {
			size_t len = (size_t)(257 - c);

			if (i + 1 > src_len || out + len > dst_cap)
				return -1;
			{
				uint8_t v = src[i++];
				size_t k;

				for (k = 0; k < len; k++)
					dst[out++] = v;
			}
		} else {
			/* c == 128: reserved no-op, never emitted by
			 * rle_encode(); tolerate it as a zero-length step for
			 * robustness against foreign input.
			 */
		}
	}
	*out_len = out;
	return 0;
}
