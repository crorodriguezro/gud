#define _POSIX_C_SOURCE 200809L

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
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define WIDTH 1280U
#define HEIGHT 720U
#define BUFFER_COUNT 2U

enum workload {
	WORKLOAD_SCROLL,
	WORKLOAD_DESKTOP,
	WORKLOAD_NOISE,
};

struct dumb_buffer {
	struct drm_mode_create_dumb create;
	void *pixels;
	uint32_t fb_id;
};

struct object_props {
	uint32_t connector_crtc_id;
	uint32_t crtc_mode_id;
	uint32_t crtc_active;
	uint32_t plane_fb_id;
	uint32_t plane_crtc_id;
	uint32_t src_x;
	uint32_t src_y;
	uint32_t src_w;
	uint32_t src_h;
	uint32_t crtc_x;
	uint32_t crtc_y;
	uint32_t crtc_w;
	uint32_t crtc_h;
};

static int report_errno(const char *operation)
{
	fprintf(stderr, "%s: %s\n", operation, strerror(errno));
	return -1;
}

static uint64_t monotonic_ns(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0;
	return (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;
}

static uint32_t next_random(uint32_t *state)
{
	uint32_t value = *state;

	value ^= value << 13;
	value ^= value >> 17;
	value ^= value << 5;
	*state = value;
	return value;
}

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
	return (uint16_t)(((uint16_t)(red & 0xf8U) << 8) |
			  ((uint16_t)(green & 0xfcU) << 3) |
			  (blue >> 3));
}

static void fill_rect(struct dumb_buffer *buffer, uint32_t x1, uint32_t y1,
		      uint32_t x2, uint32_t y2, uint16_t color)
{
	uint32_t y;

	x2 = x2 > WIDTH ? WIDTH : x2;
	y2 = y2 > HEIGHT ? HEIGHT : y2;
	for (y = y1; y < y2; y++) {
		uint16_t *line = (uint16_t *)((uint8_t *)buffer->pixels +
					      y * buffer->create.pitch);
		uint32_t x;

		for (x = x1; x < x2; x++)
			line[x] = color;
	}
}

static void paint_scroll(struct dumb_buffer *buffer, uint32_t frame)
{
	uint32_t y;

	for (y = 0; y < HEIGHT; y++) {
		uint16_t *line = (uint16_t *)((uint8_t *)buffer->pixels +
					      y * buffer->create.pitch);
		uint32_t source_y = (y + frame * 7U) % HEIGHT;
		uint32_t x;

		for (x = 0; x < WIDTH; x++) {
			uint32_t band = ((x + frame * 11U) / 80U) % 8U;
			uint8_t shade = (uint8_t)((source_y / 12U) * 3U);

			line[x] = rgb565((uint8_t)(band * 29U),
					 (uint8_t)(shade + band * 7U),
					 (uint8_t)(255U - band * 23U));
		}
	}
}

static void paint_desktop(struct dumb_buffer *buffer, uint32_t frame)
{
	uint32_t moving_x = (frame * 23U) % (WIDTH - 260U);
	uint32_t moving_y = 90U + (frame * 13U) % 330U;

	fill_rect(buffer, 0, 0, WIDTH, HEIGHT, rgb565(34, 48, 68));
	fill_rect(buffer, 0, 0, WIDTH, 52, rgb565(20, 25, 34));
	fill_rect(buffer, 0, HEIGHT - 58U, WIDTH, HEIGHT, rgb565(24, 30, 40));
	fill_rect(buffer, 70, 92, 520, 500, rgb565(224, 228, 232));
	fill_rect(buffer, 94, 130, 496, 171, rgb565(54, 98, 168));
	fill_rect(buffer, 110, 205, 460, 225, rgb565(155, 162, 170));
	fill_rect(buffer, 110, 248, 420, 268, rgb565(174, 180, 186));
	fill_rect(buffer, 110, 291, 470, 311, rgb565(142, 150, 158));
	fill_rect(buffer, 620, 120, 1180, 570, rgb565(72, 78, 88));
	fill_rect(buffer, 650, 155, 1150, 535, rgb565(14, 18, 24));
	fill_rect(buffer, moving_x, moving_y, moving_x + 260U,
		  moving_y + 150U, rgb565(238, 142, 42));
	fill_rect(buffer, moving_x + 16U, moving_y + 16U,
		  moving_x + 244U, moving_y + 134U, rgb565(246, 214, 158));
}

static void paint_noise(struct dumb_buffer *buffer, uint32_t frame)
{
	uint32_t state = 0x9e3779b9U ^ (frame * 0x85ebca6bU);
	uint32_t y;

	for (y = 0; y < HEIGHT; y++) {
		uint16_t *line = (uint16_t *)((uint8_t *)buffer->pixels +
					      y * buffer->create.pitch);
		uint32_t x;

		for (x = 0; x < WIDTH; x++)
			line[x] = (uint16_t)next_random(&state);
	}
}

static void paint_frame(struct dumb_buffer *buffer, enum workload workload,
			uint32_t frame)
{
	switch (workload) {
	case WORKLOAD_SCROLL:
		paint_scroll(buffer, frame);
		break;
	case WORKLOAD_DESKTOP:
		paint_desktop(buffer, frame);
		break;
	case WORKLOAD_NOISE:
		paint_noise(buffer, frame);
		break;
	}
}

static uint32_t get_prop_id(int fd, uint32_t object_id, uint32_t object_type,
			    const char *name)
{
	drmModeObjectProperties *properties;
	uint32_t id = 0;
	uint32_t i;

	properties = drmModeObjectGetProperties(fd, object_id, object_type);
	if (!properties)
		return 0;
	for (i = 0; i < properties->count_props; i++) {
		drmModePropertyRes *property;

		property = drmModeGetProperty(fd, properties->props[i]);
		if (!property)
			continue;
		if (strcmp(property->name, name) == 0)
			id = property->prop_id;
		drmModeFreeProperty(property);
		if (id)
			break;
	}
	drmModeFreeObjectProperties(properties);
	return id;
}

static int get_prop_value(int fd, uint32_t object_id, uint32_t object_type,
			  uint32_t property_id, uint64_t *value)
{
	drmModeObjectProperties *properties;
	uint32_t i;
	int ret = -1;

	properties = drmModeObjectGetProperties(fd, object_id, object_type);
	if (!properties)
		return -1;
	for (i = 0; i < properties->count_props; i++) {
		if (properties->props[i] == property_id) {
			*value = properties->prop_values[i];
			ret = 0;
			break;
		}
	}
	drmModeFreeObjectProperties(properties);
	return ret;
}

static int create_buffer(int fd, struct dumb_buffer *buffer)
{
	struct drm_mode_map_dumb map = { 0 };
	uint32_t handles[4] = { 0 };
	uint32_t pitches[4] = { 0 };
	uint32_t offsets[4] = { 0 };

	memset(buffer, 0, sizeof(*buffer));
	buffer->pixels = MAP_FAILED;
	buffer->create.width = WIDTH;
	buffer->create.height = HEIGHT;
	buffer->create.bpp = 16;
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &buffer->create) < 0)
		return report_errno("DRM_IOCTL_MODE_CREATE_DUMB");
	map.handle = buffer->create.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0)
		return report_errno("DRM_IOCTL_MODE_MAP_DUMB");
	buffer->pixels = mmap(NULL, buffer->create.size, PROT_READ | PROT_WRITE,
			      MAP_SHARED, fd, map.offset);
	if (buffer->pixels == MAP_FAILED)
		return report_errno("mmap");
	handles[0] = buffer->create.handle;
	pitches[0] = buffer->create.pitch;
	if (drmModeAddFB2(fd, WIDTH, HEIGHT, DRM_FORMAT_RGB565, handles,
			  pitches, offsets, &buffer->fb_id, 0) != 0)
		return report_errno("drmModeAddFB2");
	return 0;
}

static void destroy_buffer(int fd, struct dumb_buffer *buffer)
{
	struct drm_mode_destroy_dumb destroy = { 0 };

	if (buffer->fb_id)
		drmModeRmFB(fd, buffer->fb_id);
	if (buffer->pixels != MAP_FAILED)
		munmap(buffer->pixels, buffer->create.size);
	if (buffer->create.handle) {
		destroy.handle = buffer->create.handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
	}
}

static int add_modeset_properties(drmModeAtomicReq *request,
				  const struct object_props *props,
				  uint32_t connector_id, uint32_t crtc_id,
				  uint32_t plane_id, uint32_t mode_blob_id,
				  uint32_t fb_id)
{
	if (drmModeAtomicAddProperty(request, connector_id,
				     props->connector_crtc_id, crtc_id) < 0 ||
	    drmModeAtomicAddProperty(request, crtc_id, props->crtc_mode_id,
				     mode_blob_id) < 0 ||
	    drmModeAtomicAddProperty(request, crtc_id, props->crtc_active, 1) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props->plane_fb_id,
				     fb_id) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props->plane_crtc_id,
				     crtc_id) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props->src_x, 0) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props->src_y, 0) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props->src_w,
				     WIDTH << 16) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props->src_h,
				     HEIGHT << 16) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props->crtc_x, 0) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props->crtc_y, 0) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props->crtc_w, WIDTH) < 0 ||
	    drmModeAtomicAddProperty(request, plane_id, props->crtc_h, HEIGHT) < 0)
		return -1;
	return 0;
}

static int parse_workload(const char *name, enum workload *workload)
{
	if (strcmp(name, "scroll") == 0)
		*workload = WORKLOAD_SCROLL;
	else if (strcmp(name, "desktop") == 0)
		*workload = WORKLOAD_DESKTOP;
	else if (strcmp(name, "noise") == 0)
		*workload = WORKLOAD_NOISE;
	else
		return -1;
	return 0;
}

static int parse_u32(const char *text, uint32_t minimum, uint32_t maximum,
		     uint32_t *value)
{
	char *end;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(text, &end, 10);
	if (errno || !*text || *end || parsed < minimum || parsed > maximum)
		return -1;
	*value = (uint32_t)parsed;
	return 0;
}

int main(int argc, char **argv)
{
	struct dumb_buffer buffers[BUFFER_COUNT] = {
		{ .pixels = MAP_FAILED },
		{ .pixels = MAP_FAILED },
	};
	struct object_props props = { 0 };
	drmModeRes *resources = NULL;
	drmModeConnector *connector = NULL;
	drmModeEncoder *encoder = NULL;
	drmModePlaneRes *plane_resources = NULL;
	drmModePlane *plane = NULL;
	drmModeModeInfo *mode = NULL;
	enum workload workload;
	const char *card;
	const char *workload_name;
	uint64_t commit_total_ns = 0;
	uint64_t commit_min_ns = UINT64_MAX;
	uint64_t commit_max_ns = 0;
	uint64_t start_ns;
	uint64_t end_ns;
	uint64_t modeset_ns;
	uint32_t frames;
	uint32_t target_fps;
	uint32_t connector_id = 0;
	uint32_t crtc_id = 0;
	uint32_t crtc_index = 0;
	uint32_t plane_id = 0;
	uint32_t mode_blob_id = 0;
	uint32_t frame;
	int fd = -1;
	int rc = 1;
	int i;

	if (argc != 5 ||
	    parse_workload(argv[2], &workload) != 0 ||
	    parse_u32(argv[3], 2, 10000, &frames) != 0 ||
	    parse_u32(argv[4], 0, 240, &target_fps) != 0) {
		fprintf(stderr,
			"usage: %s <card> <scroll|desktop|noise> <frames:2-10000> <target-fps:0-240>\n",
			argv[0]);
		return 2;
	}
	card = argv[1];
	workload_name = argv[2];

	fd = open(card, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		report_errno("open");
		return 1;
	}
	if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0 ||
	    drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
		report_errno("drmSetClientCap");
		goto out;
	}
	resources = drmModeGetResources(fd);
	if (!resources) {
		fprintf(stderr, "drmModeGetResources failed\n");
		goto out;
	}
	for (i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *candidate;
		int mode_index;

		candidate = drmModeGetConnector(fd, resources->connectors[i]);
		if (!candidate)
			continue;
		if (candidate->connection != DRM_MODE_CONNECTED) {
			drmModeFreeConnector(candidate);
			continue;
		}
		for (mode_index = 0; mode_index < candidate->count_modes;
		     mode_index++) {
			if (candidate->modes[mode_index].hdisplay == WIDTH &&
			    candidate->modes[mode_index].vdisplay == HEIGHT) {
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
	if (!encoder && connector->count_encoders)
		encoder = drmModeGetEncoder(fd, connector->encoders[0]);
	if (!encoder) {
		fprintf(stderr, "no connector encoder\n");
		goto out;
	}
	crtc_id = encoder->crtc_id;
	for (i = 0; !crtc_id && i < resources->count_crtcs; i++) {
		if (encoder->possible_crtcs & (1U << i))
			crtc_id = resources->crtcs[i];
	}
	for (i = 0; i < resources->count_crtcs; i++) {
		if (resources->crtcs[i] == crtc_id)
			crtc_index = (uint32_t)i;
	}
	if (!crtc_id) {
		fprintf(stderr, "no compatible CRTC\n");
		goto out;
	}
	plane_resources = drmModeGetPlaneResources(fd);
	if (!plane_resources) {
		fprintf(stderr, "drmModeGetPlaneResources failed\n");
		goto out;
	}
	for (i = 0; i < (int)plane_resources->count_planes; i++) {
		drmModePlane *candidate;
		uint32_t type_property;
		uint64_t type_value;

		candidate = drmModeGetPlane(fd, plane_resources->planes[i]);
		if (!candidate)
			continue;
		if (!(candidate->possible_crtcs & (1U << crtc_index))) {
			drmModeFreePlane(candidate);
			continue;
		}
		type_property = get_prop_id(fd, candidate->plane_id,
					    DRM_MODE_OBJECT_PLANE, "type");
		if (type_property &&
		    get_prop_value(fd, candidate->plane_id,
				   DRM_MODE_OBJECT_PLANE, type_property,
				   &type_value) == 0 &&
		    type_value == DRM_PLANE_TYPE_PRIMARY) {
			plane = candidate;
			plane_id = candidate->plane_id;
			break;
		}
		drmModeFreePlane(candidate);
	}
	if (!plane) {
		fprintf(stderr, "no compatible primary plane\n");
		goto out;
	}

	props.connector_crtc_id = get_prop_id(
		fd, connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
	props.crtc_mode_id = get_prop_id(
		fd, crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
	props.crtc_active = get_prop_id(
		fd, crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");
	props.plane_fb_id = get_prop_id(
		fd, plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
	props.plane_crtc_id = get_prop_id(
		fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
	props.src_x = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X");
	props.src_y = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y");
	props.src_w = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
	props.src_h = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");
	props.crtc_x = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X");
	props.crtc_y = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
	props.crtc_w = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W");
	props.crtc_h = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H");
	if (!props.connector_crtc_id || !props.crtc_mode_id ||
	    !props.crtc_active || !props.plane_fb_id ||
	    !props.plane_crtc_id || !props.src_x || !props.src_y ||
	    !props.src_w || !props.src_h || !props.crtc_x ||
	    !props.crtc_y || !props.crtc_w || !props.crtc_h) {
		fprintf(stderr, "missing required atomic property IDs\n");
		goto out;
	}
	for (i = 0; i < (int)BUFFER_COUNT; i++) {
		if (create_buffer(fd, &buffers[i]) != 0)
			goto out;
	}
	if (drmModeCreatePropertyBlob(fd, mode, sizeof(*mode),
				      &mode_blob_id) != 0) {
		report_errno("drmModeCreatePropertyBlob");
		goto out;
	}

	paint_frame(&buffers[0], workload, 0);
	{
		drmModeAtomicReq *request = drmModeAtomicAlloc();
		uint64_t before;

		if (!request ||
		    add_modeset_properties(request, &props, connector_id,
					   crtc_id, plane_id, mode_blob_id,
					   buffers[0].fb_id) != 0) {
			fprintf(stderr, "failed to build modeset request\n");
			if (request)
				drmModeAtomicFree(request);
			goto out;
		}
		before = monotonic_ns();
		if (!before ||
		    drmModeAtomicCommit(fd, request,
					DRM_MODE_ATOMIC_ALLOW_MODESET,
					NULL) != 0) {
			report_errno("initial drmModeAtomicCommit");
			drmModeAtomicFree(request);
			goto out;
		}
		modeset_ns = monotonic_ns() - before;
		drmModeAtomicFree(request);
	}

	start_ns = monotonic_ns();
	for (frame = 1; frame < frames; frame++) {
		struct dumb_buffer *buffer = &buffers[frame % BUFFER_COUNT];
		drmModeAtomicReq *request;
		uint64_t before;
		uint64_t duration;

		paint_frame(buffer, workload, frame);
		request = drmModeAtomicAlloc();
		if (!request ||
		    drmModeAtomicAddProperty(request, plane_id,
					     props.plane_fb_id,
					     buffer->fb_id) < 0) {
			fprintf(stderr, "failed to build frame request\n");
			if (request)
				drmModeAtomicFree(request);
			goto out;
		}
		before = monotonic_ns();
		if (!before || drmModeAtomicCommit(fd, request, 0, NULL) != 0) {
			report_errno("frame drmModeAtomicCommit");
			drmModeAtomicFree(request);
			goto out;
		}
		duration = monotonic_ns() - before;
		drmModeAtomicFree(request);
		commit_total_ns += duration;
		if (duration < commit_min_ns)
			commit_min_ns = duration;
		if (duration > commit_max_ns)
			commit_max_ns = duration;

		if (target_fps) {
			uint64_t target_ns =
				start_ns +
				(uint64_t)frame * 1000000000ULL / target_fps;
			uint64_t now = monotonic_ns();

			if (target_ns > now) {
				uint64_t sleep_ns = target_ns - now;
				struct timespec sleep_time = {
					.tv_sec = sleep_ns / 1000000000ULL,
					.tv_nsec = sleep_ns % 1000000000ULL,
				};

				while (nanosleep(&sleep_time, &sleep_time) != 0 &&
				       errno == EINTR)
					;
			}
		}
	}
	end_ns = monotonic_ns();

	printf("card=%s workload=%s width=%u height=%u frames=%u target_fps=%u\n",
	       card, workload_name, WIDTH, HEIGHT, frames, target_fps);
	printf("modeset_ms=%.3f update_elapsed_s=%.3f update_fps=%.3f "
	       "commit_avg_ms=%.3f commit_min_ms=%.3f commit_max_ms=%.3f\n",
	       modeset_ns / 1000000.0,
	       (end_ns - start_ns) / 1000000000.0,
	       (frames - 1U) * 1000000000.0 / (end_ns - start_ns),
	       commit_total_ns / (frames - 1U) / 1000000.0,
	       commit_min_ns / 1000000.0,
	       commit_max_ns / 1000000.0);
	rc = 0;

out:
	if (mode_blob_id)
		drmModeDestroyPropertyBlob(fd, mode_blob_id);
	for (i = BUFFER_COUNT - 1; i >= 0; i--)
		destroy_buffer(fd, &buffers[i]);
	if (plane)
		drmModeFreePlane(plane);
	if (plane_resources)
		drmModeFreePlaneResources(plane_resources);
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
