#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#ifndef GUD_KMS_COLOR_BARS
#define GUD_KMS_COLOR_BARS 0
#endif

#ifndef GUD_KMS_HOLD_SECONDS
#define GUD_KMS_HOLD_SECONDS 0
#endif

#ifndef GUD_KMS_INITIAL_BLACK_SWITCH
#define GUD_KMS_INITIAL_BLACK_SWITCH 0
#endif

#ifndef GUD_KMS_ALTERNATING_FB_SWITCH
#define GUD_KMS_ALTERNATING_FB_SWITCH 0
#endif

#ifndef GUD_KMS_MIRGUD_FB_ORDER
#define GUD_KMS_MIRGUD_FB_ORDER 0
#endif

#if GUD_KMS_INITIAL_BLACK_SWITCH && !GUD_KMS_COLOR_BARS
#error "GUD_KMS_INITIAL_BLACK_SWITCH requires GUD_KMS_COLOR_BARS=1"
#endif

#if GUD_KMS_ALTERNATING_FB_SWITCH && !GUD_KMS_INITIAL_BLACK_SWITCH
#error "GUD_KMS_ALTERNATING_FB_SWITCH requires GUD_KMS_INITIAL_BLACK_SWITCH=1"
#endif

#if GUD_KMS_MIRGUD_FB_ORDER && !GUD_KMS_INITIAL_BLACK_SWITCH
#error "GUD_KMS_MIRGUD_FB_ORDER requires GUD_KMS_INITIAL_BLACK_SWITCH=1"
#endif

#if GUD_KMS_MIRGUD_FB_ORDER && GUD_KMS_ALTERNATING_FB_SWITCH
#error "GUD_KMS_MIRGUD_FB_ORDER and GUD_KMS_ALTERNATING_FB_SWITCH are exclusive"
#endif

enum gud_kms_pattern {
	GUD_KMS_PATTERN_BARS,
	GUD_KMS_PATTERN_OLD_DIAGNOSTIC,
	GUD_KMS_PATTERN_MIRGUD_SOLID,
};

struct object_props {
	uint32_t crtc_id;
	uint32_t mode_id;
	uint32_t active;
	uint32_t fb_id;
	uint32_t src_x;
	uint32_t src_y;
	uint32_t src_w;
	uint32_t src_h;
	uint32_t crtc_x;
	uint32_t crtc_y;
	uint32_t crtc_w;
	uint32_t crtc_h;
};

static int fail(const char *what)
{
	fprintf(stderr, "%s: %s\n", what, strerror(errno));
	return -1;
}

#if GUD_KMS_MIRGUD_FB_ORDER
static void print_timing(const char *event)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	printf("timing event=%s monotonic_ns=%llu\n", event,
	       (unsigned long long)now.tv_sec * 1000000000ULL + now.tv_nsec);
}
#endif

#if GUD_KMS_INITIAL_BLACK_SWITCH
static uint32_t mirgud_presentation_fb_id(unsigned int sequence,
					   uint32_t framebuffer_a,
					   uint32_t framebuffer_b)
{
	static const bool use_framebuffer_b[] = { false, false, true, false };

	if (sequence >= sizeof(use_framebuffer_b) /
			sizeof(use_framebuffer_b[0]))
		return 0;
	return use_framebuffer_b[sequence] ? framebuffer_b : framebuffer_a;
}
#endif

static uint32_t get_prop_id(int fd, uint32_t object_id, uint32_t object_type,
			    const char *name)
{
	drmModeObjectProperties *props;
	uint32_t id = 0;
	uint32_t i;

	props = drmModeObjectGetProperties(fd, object_id, object_type);
	if (!props)
		return 0;

	for (i = 0; i < props->count_props; i++) {
		drmModePropertyRes *prop;

		prop = drmModeGetProperty(fd, props->props[i]);
		if (!prop)
			continue;
		if (strcmp(prop->name, name) == 0)
			id = prop->prop_id;
		drmModeFreeProperty(prop);
		if (id)
			break;
	}

	drmModeFreeObjectProperties(props);
	return id;
}

static int get_prop_value(int fd, uint32_t object_id, uint32_t object_type,
			  uint32_t prop_id, uint64_t *value)
{
	drmModeObjectProperties *props;
	uint32_t i;
	int ret = -1;

	props = drmModeObjectGetProperties(fd, object_id, object_type);
	if (!props)
		return -1;

	for (i = 0; i < props->count_props; i++) {
		if (props->props[i] == prop_id) {
			*value = props->prop_values[i];
			ret = 0;
			break;
		}
	}

	drmModeFreeObjectProperties(props);
	return ret;
}

#if GUD_KMS_COLOR_BARS
static uint16_t old_diagnostic_color(uint32_t x, uint32_t y,
				     uint32_t width, uint32_t height)
{
	const uint16_t dark_gray = 0x4208;
	const uint16_t light_gray = 0xc618;
	const uint16_t orange = 0xfd20;
	const uint32_t border = 48;
	const uint32_t corner = 128;
	const uint32_t center_thickness = 24;
	const uint32_t grid_x_step = 180;
	const uint32_t grid_y_step = 240;
	const uint32_t grid_thickness = 4;
	uint32_t center_x = width / 2;
	uint32_t center_y = height / 2;
	uint16_t color = dark_gray;

	if (y < border)
		color = 0xf800;
	if (x >= width - border)
		color = 0xffe0;
	if (y >= height - border)
		color = 0x07e0;
	if (x < border)
		color = 0x001f;

	if (x < corner && y < corner)
		color = 0xffff;
	if (x >= width - corner && y < corner)
		color = 0x07ff;
	if (x < corner && y >= height - corner)
		color = 0xf81f;
	if (x >= width - corner && y >= height - corner)
		color = orange;

	if (x >= center_x - center_thickness / 2 &&
	    x < center_x + center_thickness / 2)
		color = 0xffff;
	if (y >= center_y - center_thickness / 2 &&
	    y < center_y + center_thickness / 2)
		color = 0xf81f;

	if (x % grid_x_step < grid_thickness ||
	    y % grid_y_step < grid_thickness)
		color = light_gray;

	return color;
}

static uint32_t xrgb8888_from_rgb565(uint16_t color)
{
	uint32_t red = (color >> 11) & 0x1f;
	uint32_t green = (color >> 5) & 0x3f;
	uint32_t blue = color & 0x1f;

	red = (red << 3) | (red >> 2);
	green = (green << 2) | (green >> 4);
	blue = (blue << 3) | (blue >> 2);
	return 0xff000000U | (red << 16) | (green << 8) | blue;
}

static uint16_t mirgud_solid_rgb565(void)
{
	const uint8_t red = 32;
	const uint8_t green = 128;
	const uint8_t blue = 224;

	return ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
}

static void paint_test_pattern(void *pixels, uint32_t pitch,
			       uint32_t width, uint32_t height, bool xrgb8888,
			       enum gud_kms_pattern pattern)
{
	static const uint16_t bars[] = {
		0xffff, 0xffe0, 0x07ff, 0x07e0,
		0xf81f, 0xf800, 0x001f, 0x0000,
	};
	static const uint32_t xrgb_bars[] = {
		0xffffffff, 0xffffff00, 0xff00ffff, 0xff00ff00,
		0xffff00ff, 0xffff0000, 0xff0000ff, 0xff000000,
	};
	uint32_t y;

	for (y = 0; y < height; y++) {
		uint32_t x;

		if (xrgb8888) {
			uint32_t *line = (uint32_t *)((uint8_t *)pixels + y * pitch);

			for (x = 0; x < width; x++) {
				if (pattern == GUD_KMS_PATTERN_OLD_DIAGNOSTIC)
					line[x] = xrgb8888_from_rgb565(
						old_diagnostic_color(x, y, width,
								     height));
				else
					line[x] = xrgb_bars[(x * 8) / width];
			}
		} else {
			uint16_t *line = (uint16_t *)((uint8_t *)pixels + y * pitch);

			for (x = 0; x < width; x++) {
				if (pattern == GUD_KMS_PATTERN_MIRGUD_SOLID)
					line[x] = mirgud_solid_rgb565();
				else if (pattern == GUD_KMS_PATTERN_OLD_DIAGNOSTIC)
					line[x] = old_diagnostic_color(x, y, width,
								       height);
				else
					line[x] = bars[(x * 8) / width];
			}
		}
	}
}
#endif

int main(int argc, char **argv)
{
	const char *card;
	drmModeRes *resources = NULL;
	drmModeConnector *connector = NULL;
	drmModeEncoder *encoder = NULL;
	drmModePlaneRes *plane_res = NULL;
	drmModePlane *plane = NULL;
	drmModeAtomicReq *request = NULL;
	struct drm_mode_create_dumb create = { 0 };
	struct drm_mode_map_dumb map = { 0 };
	struct drm_mode_destroy_dumb destroy = { 0 };
	struct drm_mode_create_dumb black_create = { 0 };
	struct drm_mode_destroy_dumb black_destroy = { 0 };
	struct object_props props = { 0 };
	void *pixels = MAP_FAILED;
	void *black_pixels = MAP_FAILED;
	drmModeModeInfo *mode = NULL;
	uint32_t mode_blob_id = 0;
	uint32_t fb_id = 0;
	uint32_t black_fb_id = 0;
	uint32_t connector_id = 0;
	uint32_t crtc_id = 0;
	uint32_t plane_id = 0;
	bool xrgb8888 = false;
	enum gud_kms_pattern pattern = GUD_KMS_PATTERN_BARS;
	int connector_index;
	uint32_t crtc_index = 0;
	int fd = -1;
	int rc = 1;
	int i;

	if (argc == 4 && !strcmp(argv[1], "--format") &&
	    !strcmp(argv[2], "xrgb8888")) {
		xrgb8888 = true;
		card = argv[3];
	} else if (argc == 6 && !strcmp(argv[1], "--format") &&
		   !strcmp(argv[2], "xrgb8888") &&
		   !strcmp(argv[3], "--pattern") &&
		   (!strcmp(argv[4], "old-diagnostic") ||
		    !strcmp(argv[4], "bars"))) {
		xrgb8888 = true;
		pattern = !strcmp(argv[4], "old-diagnostic") ?
			GUD_KMS_PATTERN_OLD_DIAGNOSTIC : GUD_KMS_PATTERN_BARS;
		card = argv[5];
	} else if (argc == 4 && !strcmp(argv[1], "--pattern") &&
		   !strcmp(argv[2], "mirgud-solid")) {
		pattern = GUD_KMS_PATTERN_MIRGUD_SOLID;
		card = argv[3];
	} else if (argc == 2) {
		card = argv[1];
	} else {
		fprintf(stderr,
			"usage: %s [--format xrgb8888 [--pattern bars|old-diagnostic] | --pattern mirgud-solid] <card-path>\n",
			argv[0]);
		return 1;
	}

#if !GUD_KMS_COLOR_BARS
	if (pattern == GUD_KMS_PATTERN_MIRGUD_SOLID) {
		fprintf(stderr,
			"mirgud-solid requires a build with GUD_KMS_COLOR_BARS=1\n");
		return 1;
	}
#endif

	fd = open(card, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fail("open");
		return 1;
	}

	if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0) {
		rc = fail("drmSetClientCap(DRM_CLIENT_CAP_UNIVERSAL_PLANES)");
		goto out;
	}
	if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
		rc = fail("drmSetClientCap(DRM_CLIENT_CAP_ATOMIC)");
		goto out;
	}

	resources = drmModeGetResources(fd);
	if (!resources) {
		fprintf(stderr, "drmModeGetResources failed\n");
		goto out;
	}

	for (connector_index = 0; connector_index < resources->count_connectors;
	     connector_index++) {
		drmModeConnector *candidate;
		int mode_index;

		candidate = drmModeGetConnector(fd, resources->connectors[connector_index]);
		if (!candidate)
			continue;
		if (candidate->connection != DRM_MODE_CONNECTED) {
			drmModeFreeConnector(candidate);
			continue;
		}
		for (mode_index = 0; mode_index < candidate->count_modes; mode_index++) {
			if (candidate->modes[mode_index].hdisplay == 1280 &&
			    candidate->modes[mode_index].vdisplay == 720) {
				connector = candidate;
				mode = &connector->modes[mode_index];
				break;
			}
		}
		if (connector)
			break;
		drmModeFreeConnector(candidate);
	}
	if (!connector) {
		fprintf(stderr, "no connected 1280x720 connector found\n");
		goto out;
	}
	connector_id = connector->connector_id;

	if (connector->encoder_id)
		encoder = drmModeGetEncoder(fd, connector->encoder_id);
	if (!encoder && connector->count_encoders > 0)
		encoder = drmModeGetEncoder(fd, connector->encoders[0]);
	if (!encoder) {
		fprintf(stderr, "failed to get encoder for connector %" PRIu32 "\n",
			connector_id);
		goto out;
	}

	crtc_id = encoder->crtc_id;
	if (!crtc_id) {
		for (i = 0; i < resources->count_crtcs; i++) {
			if (encoder->possible_crtcs & (1U << i)) {
				crtc_id = resources->crtcs[i];
				break;
			}
		}
	}
	if (!crtc_id) {
		fprintf(stderr, "no compatible CRTC for connector %" PRIu32 "\n",
			connector_id);
		goto out;
	}

	for (i = 0; i < resources->count_crtcs; i++) {
		if (resources->crtcs[i] == crtc_id) {
			crtc_index = (uint32_t)i;
			break;
		}
	}

	plane_res = drmModeGetPlaneResources(fd);
	if (!plane_res) {
		fprintf(stderr, "drmModeGetPlaneResources failed\n");
		goto out;
	}

	for (i = 0; i < (int)plane_res->count_planes; i++) {
		drmModePlane *candidate;
		uint32_t type_prop;
		uint64_t type_val;

		candidate = drmModeGetPlane(fd, plane_res->planes[i]);
		if (!candidate)
			continue;
		if (!(candidate->possible_crtcs & (1U << crtc_index))) {
			drmModeFreePlane(candidate);
			continue;
		}

		type_prop = get_prop_id(fd, candidate->plane_id, DRM_MODE_OBJECT_PLANE,
					"type");
		if (!type_prop ||
		    get_prop_value(fd, candidate->plane_id, DRM_MODE_OBJECT_PLANE,
				   type_prop, &type_val) != 0 ||
		    type_val != DRM_PLANE_TYPE_PRIMARY) {
			drmModeFreePlane(candidate);
			continue;
		}

		plane = candidate;
		plane_id = plane->plane_id;
		break;
	}
	if (!plane) {
		fprintf(stderr, "no compatible primary plane found\n");
		goto out;
	}

	props.crtc_id = get_prop_id(fd, connector_id, DRM_MODE_OBJECT_CONNECTOR,
				    "CRTC_ID");
	props.mode_id = get_prop_id(fd, crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
	props.active = get_prop_id(fd, crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");
	props.fb_id = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
	props.src_x = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X");
	props.src_y = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y");
	props.src_w = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
	props.src_h = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");
	props.crtc_x = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X");
	props.crtc_y = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
	props.crtc_w = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W");
	props.crtc_h = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H");
	if (!props.crtc_id || !props.mode_id || !props.active || !props.fb_id ||
	    !props.src_x || !props.src_y || !props.src_w || !props.src_h ||
	    !props.crtc_x || !props.crtc_y || !props.crtc_w || !props.crtc_h) {
		fprintf(stderr, "missing required atomic property IDs\n");
		goto out;
	}

#if GUD_KMS_INITIAL_BLACK_SWITCH
	struct drm_mode_map_dumb black_map = { 0 };

	black_create.width = 1280;
	black_create.height = 720;
	black_create.bpp = xrgb8888 ? 32 : 16;
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &black_create) < 0) {
		rc = fail("DRM_IOCTL_MODE_CREATE_DUMB(black)");
		goto out;
	}
	black_map.handle = black_create.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &black_map) < 0) {
		rc = fail("DRM_IOCTL_MODE_MAP_DUMB(black)");
		goto out;
	}
	black_pixels = mmap(NULL, black_create.size, PROT_READ | PROT_WRITE,
			    MAP_SHARED, fd, black_map.offset);
	if (black_pixels == MAP_FAILED) {
		rc = fail("mmap(black)");
		goto out;
	}
#if GUD_KMS_MIRGUD_FB_ORDER
	print_timing("initial_black_fill_begin");
#endif
	memset(black_pixels, 0, black_create.size);
#if GUD_KMS_MIRGUD_FB_ORDER
	print_timing("initial_black_fill_end");
#endif
	{
		uint32_t handles[4] = { black_create.handle, 0, 0, 0 };
		uint32_t pitches[4] = { black_create.pitch, 0, 0, 0 };
		uint32_t offsets[4] = { 0, 0, 0, 0 };

		if (drmModeAddFB2(fd, black_create.width, black_create.height,
				  xrgb8888 ? DRM_FORMAT_XRGB8888 : DRM_FORMAT_RGB565,
				  handles, pitches, offsets, &black_fb_id, 0) != 0) {
			rc = fail("drmModeAddFB2(black)");
			goto out;
		}
	}
	printf("framebuffer_a role=initial-black width=%" PRIu32
	       " height=%" PRIu32 " format=%s pitch=%" PRIu32
	       " size=%" PRIu64 " handle=%" PRIu32 " fb_id=%" PRIu32 "\n",
	       black_create.width, black_create.height,
	       xrgb8888 ? "XRGB8888" : "RGB565", black_create.pitch,
	       (uint64_t)black_create.size, black_create.handle, black_fb_id);
#endif

	create.width = 1280;
	create.height = 720;
	create.bpp = xrgb8888 ? 32 : 16;
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
		rc = fail("DRM_IOCTL_MODE_CREATE_DUMB");
		goto out;
	}
	map.handle = create.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
		rc = fail("DRM_IOCTL_MODE_MAP_DUMB");
		goto out;
	}
	pixels = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		      map.offset);
	if (pixels == MAP_FAILED) {
		rc = fail("mmap");
		goto out;
	}
#if GUD_KMS_COLOR_BARS
#if !GUD_KMS_MIRGUD_FB_ORDER
	paint_test_pattern(pixels, create.pitch, create.width, create.height,
			   xrgb8888, pattern);
#endif
#else
	memset(pixels, 0x5a, create.size);
#endif

	{
		uint32_t handles[4] = { create.handle, 0, 0, 0 };
		uint32_t pitches[4] = { create.pitch, 0, 0, 0 };
		uint32_t offsets[4] = { 0, 0, 0, 0 };

		if (drmModeAddFB2(fd, create.width, create.height,
				  xrgb8888 ? DRM_FORMAT_XRGB8888 : DRM_FORMAT_RGB565,
				  handles, pitches, offsets, &fb_id, 0) != 0) {
			rc = fail("drmModeAddFB2");
			goto out;
		}
	}
	printf("framebuffer%s width=%" PRIu32 " height=%" PRIu32
	       " format=%s pitch=%" PRIu32 " size=%" PRIu64
	       " handle=%" PRIu32 " fb_id=%" PRIu32 "\n",
	       GUD_KMS_INITIAL_BLACK_SWITCH ? "_b role=bars" : "",
	       create.width, create.height, xrgb8888 ? "XRGB8888" : "RGB565",
	       create.pitch, (uint64_t)create.size, create.handle, fb_id);

	if (drmModeCreatePropertyBlob(fd, mode, sizeof(*mode), &mode_blob_id) != 0) {
		rc = fail("drmModeCreatePropertyBlob");
		goto out;
	}

	request = drmModeAtomicAlloc();
	if (!request) {
		fprintf(stderr, "drmModeAtomicAlloc failed\n");
		goto out;
	}

	if (drmModeAtomicAddProperty(request, connector_id, props.crtc_id, crtc_id) < 0 ||
	    drmModeAtomicAddProperty(request, crtc_id, props.mode_id, mode_blob_id) < 0 ||
	    drmModeAtomicAddProperty(request, crtc_id, props.active, 1) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props.fb_id,
				     GUD_KMS_INITIAL_BLACK_SWITCH ?
				     black_fb_id : fb_id) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props.crtc_id, crtc_id) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props.src_x, 0) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props.src_y, 0) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props.src_w, 1280U << 16) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props.src_h, 720U << 16) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props.crtc_x, 0) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props.crtc_y, 0) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props.crtc_w, 1280) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props.crtc_h, 720) < 0) {
		fprintf(stderr, "drmModeAtomicAddProperty failed\n");
		goto out;
	}

	printf("atomic_modeset_properties connector=%" PRIu32
	       " CRTC_ID=%" PRIu32 ":%" PRIu32
	       " crtc=%" PRIu32 " MODE_ID=%" PRIu32 ":%" PRIu32
	       " ACTIVE=%" PRIu32 ":1"
	       " plane=%" PRIu32 " FB_ID=%" PRIu32 ":%" PRIu32
	       " plane_CRTC_ID=%" PRIu32 ":%" PRIu32
	       " SRC_X=%" PRIu32 ":0 SRC_Y=%" PRIu32 ":0"
	       " SRC_W=%" PRIu32 ":%" PRIu32
	       " SRC_H=%" PRIu32 ":%" PRIu32
	       " CRTC_X=%" PRIu32 ":0 CRTC_Y=%" PRIu32 ":0"
	       " CRTC_W=%" PRIu32 ":1280 CRTC_H=%" PRIu32 ":720"
	       " flags=%u\n",
	       connector_id, props.crtc_id, crtc_id,
	       crtc_id, props.mode_id, mode_blob_id, props.active,
	       plane_id, props.fb_id,
	       GUD_KMS_INITIAL_BLACK_SWITCH ? black_fb_id : fb_id,
	       props.crtc_id, crtc_id, props.src_x, props.src_y,
	       props.src_w, 1280U << 16, props.src_h, 720U << 16,
	       props.crtc_x, props.crtc_y, props.crtc_w, props.crtc_h,
	       DRM_MODE_ATOMIC_ALLOW_MODESET);
	if (drmModeAtomicCommit(fd, request, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL) != 0) {
		rc = fail("drmModeAtomicCommit");
		goto out;
	}

#if GUD_KMS_INITIAL_BLACK_SWITCH
#if GUD_KMS_MIRGUD_FB_ORDER
	print_timing("frame_1_fill_a_begin");
	paint_test_pattern(black_pixels, black_create.pitch, black_create.width,
			   black_create.height, xrgb8888, pattern);
	print_timing("frame_1_fill_a_end");
	printf("framebuffer_a repainted=bars while_selected=true\n");
#endif
	drmModeAtomicFree(request);
	request = drmModeAtomicAlloc();
	if (!request) {
		fprintf(stderr, "drmModeAtomicAlloc(FB-only) failed\n");
		goto out;
	}
	if (drmModeAtomicAddProperty(request, plane_id, props.fb_id,
				     GUD_KMS_MIRGUD_FB_ORDER ?
				     mirgud_presentation_fb_id(1, black_fb_id,
							      fb_id) : fb_id) < 0) {
		fprintf(stderr, "drmModeAtomicAddProperty(FB-only) failed\n");
		goto out;
	}
	printf("atomic_fb_only_properties sequence=1 plane=%" PRIu32
	       " FB_ID=%" PRIu32 ":%" PRIu32 " flags=0\n",
	       plane_id, props.fb_id,
	       GUD_KMS_MIRGUD_FB_ORDER ?
	       mirgud_presentation_fb_id(1, black_fb_id, fb_id) : fb_id);
	if (drmModeAtomicCommit(fd, request, 0, NULL) != 0) {
		rc = fail("drmModeAtomicCommit(FB-only)");
		goto out;
	}
#endif

#if GUD_KMS_MIRGUD_FB_ORDER
	print_timing("frame_2_fill_b_begin");
	paint_test_pattern(pixels, create.pitch, create.width, create.height,
			   xrgb8888, pattern);
	print_timing("frame_2_fill_b_end");
	printf("framebuffer_b painted=bars while_offscreen=true\n");

	drmModeAtomicFree(request);
	request = drmModeAtomicAlloc();
	if (!request) {
		fprintf(stderr, "drmModeAtomicAlloc(FB-only sequence 2) failed\n");
		goto out;
	}
	if (drmModeAtomicAddProperty(request, plane_id, props.fb_id,
				     mirgud_presentation_fb_id(2, black_fb_id,
							      fb_id)) < 0) {
		fprintf(stderr,
			"drmModeAtomicAddProperty(FB-only sequence 2) failed\n");
		goto out;
	}
	printf("atomic_fb_only_properties sequence=2 plane=%" PRIu32
	       " FB_ID=%" PRIu32 ":%" PRIu32 " flags=0\n",
	       plane_id, props.fb_id,
	       mirgud_presentation_fb_id(2, black_fb_id, fb_id));
	if (drmModeAtomicCommit(fd, request, 0, NULL) != 0) {
		rc = fail("drmModeAtomicCommit(FB-only sequence 2)");
		goto out;
	}

	drmModeAtomicFree(request);
	request = drmModeAtomicAlloc();
	if (!request) {
		fprintf(stderr, "drmModeAtomicAlloc(FB-only sequence 3) failed\n");
		goto out;
	}
	if (drmModeAtomicAddProperty(request, plane_id, props.fb_id,
				     mirgud_presentation_fb_id(3, black_fb_id,
							      fb_id)) < 0) {
		fprintf(stderr,
			"drmModeAtomicAddProperty(FB-only sequence 3) failed\n");
		goto out;
	}
	printf("atomic_fb_only_properties sequence=3 plane=%" PRIu32
	       " FB_ID=%" PRIu32 ":%" PRIu32 " flags=0\n",
	       plane_id, props.fb_id,
	       mirgud_presentation_fb_id(3, black_fb_id, fb_id));
	if (drmModeAtomicCommit(fd, request, 0, NULL) != 0) {
		rc = fail("drmModeAtomicCommit(FB-only sequence 3)");
		goto out;
	}
#endif

#if GUD_KMS_ALTERNATING_FB_SWITCH
	paint_test_pattern(black_pixels, black_create.pitch, black_create.width,
			   black_create.height, xrgb8888, pattern);
	printf("framebuffer_a repainted=bars while_offscreen=true\n");

	drmModeAtomicFree(request);
	request = drmModeAtomicAlloc();
	if (!request) {
		fprintf(stderr, "drmModeAtomicAlloc(FB-only sequence 2) failed\n");
		goto out;
	}
	if (drmModeAtomicAddProperty(request, plane_id, props.fb_id,
				     black_fb_id) < 0) {
		fprintf(stderr,
			"drmModeAtomicAddProperty(FB-only sequence 2) failed\n");
		goto out;
	}
	printf("atomic_fb_only_properties sequence=2 plane=%" PRIu32
	       " FB_ID=%" PRIu32 ":%" PRIu32 " flags=0\n",
	       plane_id, props.fb_id, black_fb_id);
	if (drmModeAtomicCommit(fd, request, 0, NULL) != 0) {
		rc = fail("drmModeAtomicCommit(FB-only sequence 2)");
		goto out;
	}

	drmModeAtomicFree(request);
	request = drmModeAtomicAlloc();
	if (!request) {
		fprintf(stderr, "drmModeAtomicAlloc(FB-only sequence 3) failed\n");
		goto out;
	}
	if (drmModeAtomicAddProperty(request, plane_id, props.fb_id, fb_id) < 0) {
		fprintf(stderr,
			"drmModeAtomicAddProperty(FB-only sequence 3) failed\n");
		goto out;
	}
	printf("atomic_fb_only_properties sequence=3 plane=%" PRIu32
	       " FB_ID=%" PRIu32 ":%" PRIu32 " flags=0\n",
	       plane_id, props.fb_id, fb_id);
	if (drmModeAtomicCommit(fd, request, 0, NULL) != 0) {
		rc = fail("drmModeAtomicCommit(FB-only sequence 3)");
		goto out;
	}
#endif

	printf("atomic src_raw=0,0,%" PRIu32 ",%" PRIu32
	       " src_px=0,0,1280,720 crtc=0,0,1280,720\n",
	       1280U << 16, 720U << 16);
	if (pattern == GUD_KMS_PATTERN_MIRGUD_SOLID)
		printf("pattern=mirgud-solid\n");
	else if (pattern == GUD_KMS_PATTERN_OLD_DIAGNOSTIC)
		printf("pattern=old-diagnostic\n");
	else
		printf("pattern=bars\n");
	printf("card=%s connector=%" PRIu32 " crtc=%" PRIu32 " plane=%" PRIu32
	       " dumb_handle=%" PRIu32 " fb_id=%" PRIu32 "\n",
	       card, connector_id, crtc_id, plane_id, create.handle, fb_id);
	printf("atomic modeset succeeded\n");
#if GUD_KMS_INITIAL_BLACK_SWITCH
	printf("atomic framebuffer-only switch A->B succeeded\n");
#endif
#if GUD_KMS_ALTERNATING_FB_SWITCH
	printf("atomic framebuffer-only alternating sequence B->A->B succeeded\n");
#endif
#if GUD_KMS_MIRGUD_FB_ORDER
	printf("atomic framebuffer-only mirgud sequence A->A->B->A succeeded\n");
#endif
	if (GUD_KMS_HOLD_SECONDS) {
		printf("holding test pattern for %u seconds\n",
		       (unsigned int)GUD_KMS_HOLD_SECONDS);
		sleep(GUD_KMS_HOLD_SECONDS);
	}
	rc = 0;

out:
	if (request)
		drmModeAtomicFree(request);
	if (mode_blob_id)
		drmModeDestroyPropertyBlob(fd, mode_blob_id);
	if (fb_id)
		drmModeRmFB(fd, fb_id);
	if (pixels != MAP_FAILED)
		munmap(pixels, create.size);
	if (create.handle) {
		destroy.handle = create.handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
	}
	if (black_fb_id)
		drmModeRmFB(fd, black_fb_id);
	if (black_pixels != MAP_FAILED)
		munmap(black_pixels, black_create.size);
	if (black_create.handle) {
		black_destroy.handle = black_create.handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &black_destroy);
	}
	if (plane)
		drmModeFreePlane(plane);
	if (plane_res)
		drmModeFreePlaneResources(plane_res);
	if (encoder)
		drmModeFreeEncoder(encoder);
	if (connector)
		drmModeFreeConnector(connector);
	if (resources)
		drmModeFreeResources(resources);
	if (fd >= 0)
		close(fd);

	return rc;
}
