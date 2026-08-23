#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define GUD_KMS_COLOR_BARS 1
#define GUD_KMS_INITIAL_BLACK_SWITCH 1
#define GUD_KMS_MIRGUD_FB_ORDER 1
#define GUD_KMS_HOLD_SECONDS 0
#define main gud_kms_smoke_main
#include "gud-kms-smoke.c"
#undef main

static int failures;

static void expect_u16(const char *what, uint16_t actual, uint16_t expected)
{
	if (actual == expected)
		return;

	fprintf(stderr, "FAIL %s: actual=0x%04x expected=0x%04x\n",
		what, actual, expected);
	failures++;
}

static void expect_u32(const char *what, uint32_t actual, uint32_t expected)
{
	if (actual == expected)
		return;

	fprintf(stderr, "FAIL %s: actual=%u expected=%u\n",
		what, actual, expected);
	failures++;
}

static void expect_pixel(const uint32_t *pixels, uint32_t width,
			 uint32_t x, uint32_t y, uint32_t expected)
{
	uint32_t actual = pixels[y * width + x];

	if (actual == expected)
		return;

	fprintf(stderr,
		"FAIL pixel (%u,%u): actual=0x%08x expected=0x%08x\n",
		x, y, actual, expected);
	failures++;
}

static void test_mirgud_solid_rgb565(void)
{
	const uint32_t width = 1280;
	const uint32_t height = 720;
	const uint32_t row_bytes = width * 2;
	const uint32_t pitch = row_bytes + 16;
	uint8_t *pixels = malloc((size_t)height * pitch);
	uint32_t y;

	if (!pixels) {
		failures++;
		return;
	}

	memset(pixels, 0xa5, (size_t)height * pitch);
	paint_test_pattern(pixels, pitch, width, height, false,
			   GUD_KMS_PATTERN_MIRGUD_SOLID);
	expect_u16("mirgud solid RGB565 value", mirgud_solid_rgb565(), 0x241c);

	for (y = 0; y < height; y++) {
		uint32_t x;

		for (x = 0; x < width; x++) {
			size_t offset = (size_t)y * pitch + x * 2;

			if (pixels[offset] != 0x1c || pixels[offset + 1] != 0x24) {
				fprintf(stderr,
					"FAIL mirgud solid byte (%u,%u): actual=%02x %02x expected=1c 24\n",
					x, y, pixels[offset], pixels[offset + 1]);
				failures++;
				goto out;
			}
		}
		for (x = row_bytes; x < pitch; x++) {
			if (pixels[(size_t)y * pitch + x] != 0xa5) {
				fprintf(stderr,
					"FAIL mirgud solid padding row=%u offset=%u\n",
					y, x);
				failures++;
				goto out;
			}
		}
	}

out:
	free(pixels);
}

static void test_mirgud_presentation_order(void)
{
	const uint32_t framebuffer_a = 30;
	const uint32_t framebuffer_b = 31;

	expect_u32("initial black framebuffer",
		   mirgud_presentation_fb_id(0, framebuffer_a, framebuffer_b),
		   framebuffer_a);
	expect_u32("first content recommits framebuffer A",
		   mirgud_presentation_fb_id(1, framebuffer_a, framebuffer_b),
		   framebuffer_a);
	expect_u32("second content switches to framebuffer B",
		   mirgud_presentation_fb_id(2, framebuffer_a, framebuffer_b),
		   framebuffer_b);
	expect_u32("third content returns to framebuffer A",
		   mirgud_presentation_fb_id(3, framebuffer_a, framebuffer_b),
		   framebuffer_a);
	expect_u32("out-of-range presentation sequence rejected",
		   mirgud_presentation_fb_id(4, framebuffer_a, framebuffer_b), 0);
}

int main(void)
{
	const uint32_t width = 1280;
	const uint32_t height = 720;
	const uint32_t pitch = width * 4;
	uint32_t *pixels = calloc(height, pitch);

	if (!pixels)
		return 2;

	paint_test_pattern(pixels, pitch, width, height, true,
			   GUD_KMS_PATTERN_OLD_DIAGNOSTIC);
	expect_pixel(pixels, width, 200, 200, 0xff424142);
	expect_pixel(pixels, width, 300, 20, 0xffff0000);
	expect_pixel(pixels, width, 1250, 200, 0xffffff00);
	expect_pixel(pixels, width, 300, 700, 0xff00ff00);
	expect_pixel(pixels, width, 20, 200, 0xff0000ff);
	expect_pixel(pixels, width, 50, 50, 0xffffffff);
	expect_pixel(pixels, width, 1200, 50, 0xff00ffff);
	expect_pixel(pixels, width, 50, 650, 0xffff00ff);
	expect_pixel(pixels, width, 1200, 650, 0xffffa600);
	expect_pixel(pixels, width, 640, 200, 0xffffffff);
	expect_pixel(pixels, width, 300, 360, 0xffff00ff);
	expect_pixel(pixels, width, 180, 200, 0xffc6c3c6);
	test_mirgud_solid_rgb565();
	test_mirgud_presentation_order();

	free(pixels);
	if (failures)
		return 1;

	puts("gud kms historical pattern tests: PASS");
	return 0;
}
