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
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define BUFFER_COUNT 2U
#define GUD_DISPLAY_MODE_FLAG_USER_MASK 0x000033ffU
#define MODE_KEY_FIELDS 10U

struct mode_key {
	uint32_t clock;
	uint16_t hdisplay;
	uint16_t hsync_start;
	uint16_t hsync_end;
	uint16_t htotal;
	uint16_t vdisplay;
	uint16_t vsync_start;
	uint16_t vsync_end;
	uint16_t vtotal;
	uint32_t flags;
};

struct selected_mode {
	uint32_t connector_id;
	drmModeModeInfo mode;
};

struct dumb_buffer {
	struct drm_mode_create_dumb create;
	void *pixels;
	uint32_t fb_id;
};

struct paint_stats {
	uint64_t rows;
	uint64_t visible_bytes;
};

static void usage(FILE *stream, const char *program)
{
	fprintf(stream,
		"usage: %s --device PATH --list-modes\n"
		"       %s --device PATH --mode-key "
		"CLOCK:HDISPLAY:HSYNC_START:HSYNC_END:HTOTAL:"
		"VDISPLAY:VSYNC_START:VSYNC_END:VTOTAL:FLAGS "
		"--frames N --pattern row-id --json PATH\n",
		program, program);
}

static int report_errno(const char *operation)
{
	fprintf(stderr, "%s: %s\n", operation, strerror(errno));
	return -1;
}

static uint32_t normalized_flags(uint32_t flags)
{
	return flags & GUD_DISPLAY_MODE_FLAG_USER_MASK;
}

static struct mode_key key_from_mode(const drmModeModeInfo *mode)
{
	struct mode_key key = {
		.clock = mode->clock,
		.hdisplay = mode->hdisplay,
		.hsync_start = mode->hsync_start,
		.hsync_end = mode->hsync_end,
		.htotal = mode->htotal,
		.vdisplay = mode->vdisplay,
		.vsync_start = mode->vsync_start,
		.vsync_end = mode->vsync_end,
		.vtotal = mode->vtotal,
		.flags = normalized_flags(mode->flags),
	};

	return key;
}

static bool keys_equal(const struct mode_key *left, const struct mode_key *right)
{
	return left->clock == right->clock &&
	       left->hdisplay == right->hdisplay &&
	       left->hsync_start == right->hsync_start &&
	       left->hsync_end == right->hsync_end &&
	       left->htotal == right->htotal &&
	       left->vdisplay == right->vdisplay &&
	       left->vsync_start == right->vsync_start &&
	       left->vsync_end == right->vsync_end &&
	       left->vtotal == right->vtotal &&
	       left->flags == right->flags;
}

static int format_mode_key(const struct mode_key *key, char *buffer, size_t size)
{
	int length;

	length = snprintf(buffer, size,
			  "%" PRIu32 ":%" PRIu16 ":%" PRIu16 ":%" PRIu16
			  ":%" PRIu16 ":%" PRIu16 ":%" PRIu16 ":%" PRIu16
			  ":%" PRIu16 ":0x%08" PRIx32,
			  key->clock, key->hdisplay, key->hsync_start,
			  key->hsync_end, key->htotal, key->vdisplay,
			  key->vsync_start, key->vsync_end, key->vtotal,
			  key->flags);
	if (length < 0 || (size_t)length >= size)
		return -1;
	return 0;
}

static int parse_unsigned(const char *text, uint64_t maximum, uint64_t *value)
{
	char *end;
	unsigned long long parsed;

	if (!text[0] || text[0] == '-')
		return -1;
	errno = 0;
	parsed = strtoull(text, &end, 0);
	if (errno || *end || parsed > maximum)
		return -1;
	*value = parsed;
	return 0;
}

static int parse_mode_key(const char *text, struct mode_key *key)
{
	char *copy;
	char *save = NULL;
	char *field;
	uint64_t values[MODE_KEY_FIELDS];
	size_t count = 0;
	int rc = -1;

	copy = strdup(text);
	if (!copy)
		return -1;
	for (field = strtok_r(copy, ":", &save); field;
	     field = strtok_r(NULL, ":", &save)) {
		uint64_t maximum;

		if (count >= MODE_KEY_FIELDS)
			goto out;
		maximum = count == 0 || count == MODE_KEY_FIELDS - 1 ?
				  UINT32_MAX : UINT16_MAX;
		if (parse_unsigned(field, maximum, &values[count]))
			goto out;
		count++;
	}
	if (count != MODE_KEY_FIELDS)
		goto out;
	key->clock = values[0];
	key->hdisplay = values[1];
	key->hsync_start = values[2];
	key->hsync_end = values[3];
	key->htotal = values[4];
	key->vdisplay = values[5];
	key->vsync_start = values[6];
	key->vsync_end = values[7];
	key->vtotal = values[8];
	key->flags = normalized_flags(values[9]);
	if (!key->clock || !key->hdisplay || !key->vdisplay)
		goto out;
	rc = 0;
out:
	free(copy);
	return rc;
}

static size_t count_mode_matches(const drmModeModeInfo *modes, size_t count,
				 const struct mode_key *wanted)
{
	size_t matches = 0;
	size_t index;

	for (index = 0; index < count; index++) {
		struct mode_key candidate = key_from_mode(&modes[index]);

		if (keys_equal(&candidate, wanted))
			matches++;
	}
	return matches;
}

static int checked_mapping_size(size_t pitch, uint32_t height, size_t *size)
{
	if (!pitch || !height || height > SIZE_MAX / pitch)
		return -1;
	*size = pitch * height;
	return 0;
}

static int paint_row_id(struct dumb_buffer *buffer, uint32_t width,
			uint32_t height, uint32_t frame,
			struct paint_stats *stats)
{
	size_t mapping_size;
	uint32_t y;

	if (checked_mapping_size(buffer->create.pitch, height, &mapping_size))
		return -1;
	if (buffer->create.size < mapping_size ||
	    buffer->create.pitch < width * sizeof(uint16_t))
		return -1;
	stats->rows = 0;
	stats->visible_bytes = 0;
	for (y = 0; y < height; y++) {
		uint16_t *row = (uint16_t *)((uint8_t *)buffer->pixels +
					     y * buffer->create.pitch);
		uint16_t row_id = (uint16_t)(y ^ (frame * 0x9e37U));
		uint32_t x;

		for (x = 0; x < width; x++)
			row[x] = (uint16_t)(row_id ^ (uint16_t)(x * 0x21U));
		stats->rows++;
		stats->visible_bytes += (uint64_t)width * sizeof(uint16_t);
	}
	return 0;
}

static int create_buffer(int fd, uint32_t width, uint32_t height,
			 struct dumb_buffer *buffer)
{
	uint32_t handles[4] = { 0 };
	uint32_t pitches[4] = { 0 };
	uint32_t offsets[4] = { 0 };
	struct drm_mode_map_dumb map = { 0 };

	memset(buffer, 0, sizeof(*buffer));
	buffer->create.width = width;
	buffer->create.height = height;
	buffer->create.bpp = 16;
	if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &buffer->create) != 0)
		return report_errno("DRM_IOCTL_MODE_CREATE_DUMB");
	if (buffer->create.pitch != width * sizeof(uint16_t)) {
		fprintf(stderr, "RGB565 pitch is %" PRIu32 ", expected %" PRIu32 "\n",
			buffer->create.pitch, width * (uint32_t)sizeof(uint16_t));
		return -1;
	}
	handles[0] = buffer->create.handle;
	pitches[0] = buffer->create.pitch;
	if (drmModeAddFB2(fd, width, height, DRM_FORMAT_RGB565, handles,
			  pitches, offsets, &buffer->fb_id, 0) != 0)
		return report_errno("drmModeAddFB2");
	map.handle = buffer->create.handle;
	if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0)
		return report_errno("DRM_IOCTL_MODE_MAP_DUMB");
	buffer->pixels = mmap(NULL, buffer->create.size, PROT_READ | PROT_WRITE,
			      MAP_SHARED, fd, map.offset);
	if (buffer->pixels == MAP_FAILED) {
		buffer->pixels = NULL;
		return report_errno("mmap dumb buffer");
	}
	return 0;
}

static void destroy_buffer(int fd, struct dumb_buffer *buffer)
{
	struct drm_mode_destroy_dumb destroy = {
		.handle = buffer->create.handle,
	};

	if (buffer->pixels)
		munmap(buffer->pixels, buffer->create.size);
	if (buffer->fb_id)
		drmModeRmFB(fd, buffer->fb_id);
	if (buffer->create.handle)
		ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
	memset(buffer, 0, sizeof(*buffer));
}

static int list_modes(int fd)
{
	drmModeRes *resources;
	int found = 0;
	int connector_index;

	resources = drmModeGetResources(fd);
	if (!resources)
		return report_errno("drmModeGetResources");
	for (connector_index = 0; connector_index < resources->count_connectors;
	     connector_index++) {
		drmModeConnector *connector =
			drmModeGetConnector(fd, resources->connectors[connector_index]);
		int mode_index;

		if (!connector)
			continue;
		if (connector->connection != DRM_MODE_CONNECTED) {
			drmModeFreeConnector(connector);
			continue;
		}
		for (mode_index = 0; mode_index < connector->count_modes; mode_index++) {
			struct mode_key key = key_from_mode(&connector->modes[mode_index]);
			char key_text[192];

			if (format_mode_key(&key, key_text, sizeof(key_text)))
				continue;
			printf("{\"connector_id\":%" PRIu32
			       ",\"mode_index\":%d,\"name\":\"%s\","
			       "\"mode_key\":\"%s\"}\n",
			       connector->connector_id, mode_index,
			       connector->modes[mode_index].name, key_text);
			found++;
		}
		drmModeFreeConnector(connector);
	}
	drmModeFreeResources(resources);
	if (!found) {
		fprintf(stderr, "no connected DRM connector modes found\n");
		return -1;
	}
	return 0;
}

static int select_unique_mode(int fd, const struct mode_key *wanted,
			      struct selected_mode *selected)
{
	drmModeRes *resources;
	size_t matches = 0;
	int connector_index;

	resources = drmModeGetResources(fd);
	if (!resources)
		return report_errno("drmModeGetResources");
	for (connector_index = 0; connector_index < resources->count_connectors;
	     connector_index++) {
		drmModeConnector *connector =
			drmModeGetConnector(fd, resources->connectors[connector_index]);
		int mode_index;

		if (!connector)
			continue;
		if (connector->connection != DRM_MODE_CONNECTED) {
			drmModeFreeConnector(connector);
			continue;
		}
		for (mode_index = 0; mode_index < connector->count_modes; mode_index++) {
			struct mode_key candidate =
				key_from_mode(&connector->modes[mode_index]);

			if (!keys_equal(&candidate, wanted))
				continue;
			selected->connector_id = connector->connector_id;
			selected->mode = connector->modes[mode_index];
			matches++;
		}
		drmModeFreeConnector(connector);
	}
	drmModeFreeResources(resources);
	if (matches != 1) {
		fprintf(stderr,
			"complete timing selector matched %zu modes; expected exactly one\n",
			matches);
		return -1;
	}
	return 0;
}

static int find_crtc(int fd, uint32_t connector_id, uint32_t *crtc_id)
{
	drmModeRes *resources = NULL;
	drmModeConnector *connector = NULL;
	int rc = -1;
	int encoder_index;

	resources = drmModeGetResources(fd);
	connector = drmModeGetConnector(fd, connector_id);
	if (!resources || !connector) {
		report_errno("read connector/CRTC resources");
		goto out;
	}
	for (encoder_index = -1; encoder_index < connector->count_encoders;
	     encoder_index++) {
		uint32_t encoder_id = encoder_index < 0 ?
					      connector->encoder_id :
					      connector->encoders[encoder_index];
		drmModeEncoder *encoder;
		int crtc_index;

		if (!encoder_id)
			continue;
		encoder = drmModeGetEncoder(fd, encoder_id);
		if (!encoder)
			continue;
		if (encoder->crtc_id) {
			*crtc_id = encoder->crtc_id;
			drmModeFreeEncoder(encoder);
			rc = 0;
			goto out;
		}
		for (crtc_index = 0; crtc_index < resources->count_crtcs;
		     crtc_index++) {
			if (!(encoder->possible_crtcs & (1U << crtc_index)))
				continue;
			*crtc_id = resources->crtcs[crtc_index];
			drmModeFreeEncoder(encoder);
			rc = 0;
			goto out;
		}
		drmModeFreeEncoder(encoder);
	}
	fprintf(stderr, "no compatible CRTC found for connector %" PRIu32 "\n",
		connector_id);
out:
	if (connector)
		drmModeFreeConnector(connector);
	if (resources)
		drmModeFreeResources(resources);
	return rc;
}

static void json_string(FILE *stream, const char *text)
{
	const unsigned char *cursor = (const unsigned char *)text;

	fputc('"', stream);
	for (; *cursor; cursor++) {
		if (*cursor == '"' || *cursor == '\\')
			fputc('\\', stream);
		if (*cursor >= 0x20)
			fputc(*cursor, stream);
	}
	fputc('"', stream);
}

static int write_result(const char *path, const char *device,
			const struct selected_mode *selected, uint32_t crtc_id,
			const struct dumb_buffer *buffer, uint32_t frames,
			uint64_t rows_total, uint64_t bytes_total)
{
	struct mode_key key = key_from_mode(&selected->mode);
	char key_text[192];
	FILE *stream;

	if (format_mode_key(&key, key_text, sizeof(key_text)))
		return -1;
	stream = strcmp(path, "-") == 0 ? stdout : fopen(path, "w");
	if (!stream)
		return report_errno("open JSON result");
	fputs("{\"device\":", stream);
	json_string(stream, device);
	fprintf(stream,
		",\"connector_id\":%" PRIu32 ",\"crtc_id\":%" PRIu32
		",\"mode_key\":\"%s\",\"width\":%" PRIu16
		",\"height\":%" PRIu16 ",\"pitch\":%" PRIu32
		",\"frames\":%" PRIu32 ",\"pattern\":\"row-id\""
		",\"rows_per_frame\":%" PRIu16 ",\"rows_covered_total\":%" PRIu64
		",\"source_bytes_per_frame\":%" PRIu64
		",\"source_bytes_total\":%" PRIu64 "}\n",
		selected->connector_id, crtc_id, key_text,
		selected->mode.hdisplay, selected->mode.vdisplay,
		buffer->create.pitch, frames, selected->mode.vdisplay, rows_total,
		(uint64_t)selected->mode.hdisplay * selected->mode.vdisplay *
			sizeof(uint16_t),
		bytes_total);
	if (stream != stdout && fclose(stream) != 0)
		return report_errno("close JSON result");
	return 0;
}

static int self_test(void)
{
	const char *valid =
		"74250:1280:1390:1430:1650:720:725:730:750:0x00000005";
	struct mode_key key;
	drmModeModeInfo modes[2] = { 0 };
	struct dumb_buffer buffer = { 0 };
	struct paint_stats stats;
	size_t allocation_size;
	uint8_t pixels[72];

	if (parse_mode_key(valid, &key) ||
	    parse_mode_key("74250:1280:720", &key) == 0 ||
	    parse_mode_key("-1:1280:1390:1430:1650:720:725:730:750:0", &key) == 0)
		return 1;
	modes[0].clock = key.clock;
	modes[0].hdisplay = key.hdisplay;
	modes[0].hsync_start = key.hsync_start;
	modes[0].hsync_end = key.hsync_end;
	modes[0].htotal = key.htotal;
	modes[0].vdisplay = key.vdisplay;
	modes[0].vsync_start = key.vsync_start;
	modes[0].vsync_end = key.vsync_end;
	modes[0].vtotal = key.vtotal;
	modes[0].flags = key.flags | 0x80000000U;
	modes[1] = modes[0];
	if (count_mode_matches(modes, 1, &key) != 1 ||
	    count_mode_matches(modes, 2, &key) != 2) {
		return 1;
	}
	key.clock++;
	if (count_mode_matches(modes, 2, &key) != 0)
		return 1;
	if (checked_mapping_size(SIZE_MAX, 2, &allocation_size) == 0)
		return 1;
	memset(pixels, 0xa5, sizeof(pixels));
	buffer.create.width = 8;
	buffer.create.height = 4;
	buffer.create.pitch = 18;
	buffer.create.size = sizeof(pixels);
	buffer.pixels = pixels;
	if (paint_row_id(&buffer, 8, 4, 0, &stats) ||
	    stats.rows != 4 || stats.visible_bytes != 64)
		return 1;
	if (((uint16_t *)pixels)[0] != 0 ||
	    *(uint16_t *)(pixels + 3 * buffer.create.pitch) != 3)
		return 1;
	printf("{\"self_test\":\"ok\",\"rows\":%" PRIu64
	       ",\"bytes\":%" PRIu64 "}\n",
	       stats.rows, stats.visible_bytes);
	return 0;
}

int main(int argc, char **argv)
{
	const char *device = NULL;
	const char *mode_key_text = NULL;
	const char *pattern = NULL;
	const char *json_path = NULL;
	bool list = false;
	bool run_self_test = false;
	uint32_t frames = 0;
	struct mode_key wanted;
	struct selected_mode selected = { 0 };
	struct dumb_buffer buffers[BUFFER_COUNT] = { 0 };
	uint32_t crtc_id = 0;
	uint64_t rows_total = 0;
	uint64_t bytes_total = 0;
	int fd = -1;
	int rc = 1;
	int index;
	uint32_t frame;

	for (index = 1; index < argc; index++) {
		if (strcmp(argv[index], "--device") == 0 && index + 1 < argc)
			device = argv[++index];
		else if (strcmp(argv[index], "--list-modes") == 0)
			list = true;
		else if (strcmp(argv[index], "--mode-key") == 0 &&
			 index + 1 < argc)
			mode_key_text = argv[++index];
		else if (strcmp(argv[index], "--frames") == 0 &&
			 index + 1 < argc) {
			uint64_t parsed;

			if (parse_unsigned(argv[++index], UINT32_MAX, &parsed) ||
			    !parsed) {
				usage(stderr, argv[0]);
				return 2;
			}
			frames = parsed;
		} else if (strcmp(argv[index], "--pattern") == 0 &&
			   index + 1 < argc)
			pattern = argv[++index];
		else if (strcmp(argv[index], "--json") == 0 &&
			   index + 1 < argc)
			json_path = argv[++index];
		else if (strcmp(argv[index], "--self-test") == 0)
			run_self_test = true;
		else {
			usage(stderr, argv[0]);
			return 2;
		}
	}
	if (run_self_test)
		return self_test();
	if (!device || list == (mode_key_text != NULL)) {
		usage(stderr, argv[0]);
		return 2;
	}
	if (!list &&
	    (!frames || !pattern || strcmp(pattern, "row-id") != 0 || !json_path)) {
		usage(stderr, argv[0]);
		return 2;
	}
	fd = open(device, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return report_errno("open DRM device");
	if (list) {
		rc = list_modes(fd) == 0 ? 0 : 1;
		goto out;
	}
	if (parse_mode_key(mode_key_text, &wanted)) {
		fprintf(stderr, "invalid complete timing mode key\n");
		rc = 2;
		goto out;
	}
	if (select_unique_mode(fd, &wanted, &selected) ||
	    find_crtc(fd, selected.connector_id, &crtc_id))
		goto out;
	for (index = 0; index < (int)BUFFER_COUNT; index++) {
		if (create_buffer(fd, selected.mode.hdisplay,
				  selected.mode.vdisplay, &buffers[index]))
			goto out;
	}
	for (frame = 0; frame < frames; frame++) {
		struct dumb_buffer *buffer = &buffers[frame % BUFFER_COUNT];
		struct paint_stats stats;

		if (paint_row_id(buffer, selected.mode.hdisplay,
				 selected.mode.vdisplay, frame, &stats))
			goto out;
		if (drmModeSetCrtc(fd, crtc_id, buffer->fb_id, 0, 0,
				   &selected.connector_id, 1, &selected.mode) != 0) {
			report_errno("drmModeSetCrtc");
			goto out;
		}
		rows_total += stats.rows;
		bytes_total += stats.visible_bytes;
	}
	if (write_result(json_path, device, &selected, crtc_id,
			 &buffers[(frames - 1) % BUFFER_COUNT], frames,
			 rows_total, bytes_total))
		goto out;
	rc = 0;
out:
	for (index = BUFFER_COUNT - 1; index >= 0; index--)
		destroy_buffer(fd, &buffers[index]);
	if (fd >= 0)
		close(fd);
	return rc;
}
