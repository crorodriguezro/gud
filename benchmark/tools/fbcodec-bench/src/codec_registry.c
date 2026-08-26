/*
 * codec_registry.c - the full static list of benchmark candidates, in a
 * stable, documented order matching the "suggested initial implementation
 * order" from PROJECT SPEC section 32 (baselines -> spatial -> quant ->
 * combos -> temporal -> shuffle/RLE -> external codecs).
 */
#include <string.h>

#include "codec.h"

extern const fbcodec_desc fbcodec_raw;
extern const fbcodec_desc fbcodec_rgb565_lz4;

extern const fbcodec_desc fbcodec_sub_byte_lz4;
extern const fbcodec_desc fbcodec_u16_sub_lz4;
extern const fbcodec_desc fbcodec_u16_xor_lz4;
extern const fbcodec_desc fbcodec_paeth_byte_lz4;
extern const fbcodec_desc fbcodec_paeth_pixel_lz4;
extern const fbcodec_desc fbcodec_med_lz4;
extern const fbcodec_desc fbcodec_channel_sub_lz4;
extern const fbcodec_desc fbcodec_channel_xor_lz4;
extern const fbcodec_desc fbcodec_shuffle_lz4;

extern const fbcodec_desc fbcodec_quant_mild_raw;
extern const fbcodec_desc fbcodec_quant_mild_lz4;
extern const fbcodec_desc fbcodec_quant_moderate_16_lz4;
extern const fbcodec_desc fbcodec_quant_moderate_packed_lz4;
extern const fbcodec_desc fbcodec_quant_aggressive_raw;
extern const fbcodec_desc fbcodec_quant_aggressive_lz4;

extern const fbcodec_desc fbcodec_quant_mild_u16sub_lz4;
extern const fbcodec_desc fbcodec_quant_mild_u16xor_lz4;
extern const fbcodec_desc fbcodec_quant_moderate_bestpred_lz4;
extern const fbcodec_desc fbcodec_quant_shuffle_lz4;

extern const fbcodec_desc fbcodec_prev_xor_lz4;
extern const fbcodec_desc fbcodec_prev_sub_lz4;
extern const fbcodec_desc fbcodec_prev_quant_xor_lz4;

extern const fbcodec_desc fbcodec_rle_lz4;

extern const fbcodec_desc fbcodec_qoi;
extern const fbcodec_desc fbcodec_qoir_lossless;
extern const fbcodec_desc fbcodec_qoir_lossy3;
extern const fbcodec_desc fbcodec_qoir_lossy5;
#ifndef FBCODEC_NO_CHARLS
extern const fbcodec_desc fbcodec_charls_lossless;
extern const fbcodec_desc fbcodec_charls_near1;
extern const fbcodec_desc fbcodec_charls_near3;
#endif

static const fbcodec_desc *const g_registry[] = {
	&fbcodec_raw,
	&fbcodec_rgb565_lz4,

	&fbcodec_sub_byte_lz4,
	&fbcodec_u16_sub_lz4,
	&fbcodec_u16_xor_lz4,
	&fbcodec_paeth_byte_lz4,
	&fbcodec_paeth_pixel_lz4,
	&fbcodec_med_lz4,
	&fbcodec_channel_sub_lz4,
	&fbcodec_channel_xor_lz4,
	&fbcodec_shuffle_lz4,

	&fbcodec_quant_mild_raw,
	&fbcodec_quant_mild_lz4,
	&fbcodec_quant_moderate_16_lz4,
	&fbcodec_quant_moderate_packed_lz4,
	&fbcodec_quant_aggressive_raw,
	&fbcodec_quant_aggressive_lz4,

	&fbcodec_quant_mild_u16sub_lz4,
	&fbcodec_quant_mild_u16xor_lz4,
	&fbcodec_quant_moderate_bestpred_lz4,
	&fbcodec_quant_shuffle_lz4,

	&fbcodec_prev_xor_lz4,
	&fbcodec_prev_sub_lz4,
	&fbcodec_prev_quant_xor_lz4,

	&fbcodec_rle_lz4,

	&fbcodec_qoi,
	&fbcodec_qoir_lossless,
	&fbcodec_qoir_lossy3,
	&fbcodec_qoir_lossy5,
#ifndef FBCODEC_NO_CHARLS
	&fbcodec_charls_lossless,
	&fbcodec_charls_near1,
	&fbcodec_charls_near3,
#endif
};

#define G_REGISTRY_COUNT (sizeof(g_registry) / sizeof(g_registry[0]))

size_t fbcodec_registry(const fbcodec_desc **out, size_t max)
{
	size_t n = G_REGISTRY_COUNT;
	size_t i;

	if (n > max)
		n = max;
	for (i = 0; i < n; i++)
		out[i] = g_registry[i];
	return n;
}

const fbcodec_desc *fbcodec_find(const char *name)
{
	size_t i;

	for (i = 0; i < G_REGISTRY_COUNT; i++) {
		if (strcmp(g_registry[i]->name, name) == 0)
			return g_registry[i];
	}
	return NULL;
}
