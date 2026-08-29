#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct object_props {
	uint32_t crtc_id, mode_id, active, fb_id;
	uint32_t src_x, src_y, src_w, src_h;
	uint32_t crtc_x, crtc_y, crtc_w, crtc_h;
};

static int fail(const char *what)
{
	fprintf(stderr, "%s: %s\n", what, strerror(errno));
	return 1;
}

static uint32_t get_prop_id(int fd, uint32_t object_id, uint32_t object_type,
			    const char *name)
{
	drmModeObjectProperties *props;
	uint32_t id = 0, i;

	props = drmModeObjectGetProperties(fd, object_id, object_type);
	if (!props)
		return 0;
	for (i = 0; i < props->count_props; i++) {
		drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);

		if (prop && !strcmp(prop->name, name))
			id = prop->prop_id;
		if (prop)
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

int main(int argc, char **argv)
{
	const char *stage;
	drmModeRes *resources = NULL;
	drmModeConnector *connector = NULL;
	drmModeEncoder *encoder = NULL;
	drmModePlaneRes *plane_res = NULL;
	drmModePlane *plane = NULL;
	drmModeAtomicReq *request = NULL;
	struct drm_mode_create_dumb create = { 0 };
	struct drm_mode_map_dumb map = { 0 };
	struct drm_mode_destroy_dumb destroy = { 0 };
	struct object_props props = { 0 };
	drmModeModeInfo *mode = NULL;
	void *pixels = MAP_FAILED;
	uint32_t fb_id = 0, mode_blob_id = 0;
	uint32_t connector_id = 0, crtc_id = 0, plane_id = 0, crtc_index = 0;
	int fd = -1, i, rc = 1;

	if (argc != 3) {
		fprintf(stderr,
			"usage: %s <caps|dumb|fb|resources|connector|encoder-crtc|planes|properties|atomic-build|atomic-test|atomic-commit> <card-path>\n",
			argv[0]);
		return 2;
	}
	stage = argv[1];
	if (strcmp(stage, "caps") && strcmp(stage, "dumb") &&
	    strcmp(stage, "fb") && strcmp(stage, "resources") &&
	    strcmp(stage, "connector") && strcmp(stage, "encoder-crtc") &&
	    strcmp(stage, "planes") && strcmp(stage, "properties") &&
	    strcmp(stage, "atomic-build") && strcmp(stage, "atomic-test") &&
	    strcmp(stage, "atomic-commit")) {
		fprintf(stderr, "invalid stage: %s\n", stage);
		return 2;
	}

	fd = open(argv[2], O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return fail("open");
	if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
		rc = fail("DRM_CLIENT_CAP_UNIVERSAL_PLANES");
		goto out;
	}
	if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
		rc = fail("DRM_CLIENT_CAP_ATOMIC");
		goto out;
	}
	if (!strcmp(stage, "caps")) {
		printf("stage=%s complete\n", stage);
		rc = 0;
		goto out;
	}

	create.width = 1280;
	create.height = 720;
	/* The validated production gadget advertises packed RGB565. */
	create.bpp = 16;
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
	memset(pixels, 0x5a, create.size);
	if (strcmp(stage, "dumb") == 0) {
		printf("stage=%s complete\n", stage);
		rc = 0;
		goto out;
	}

	{
		uint32_t handles[4] = { create.handle };
		uint32_t pitches[4] = { create.pitch };
		uint32_t offsets[4] = { 0 };

		if (drmModeAddFB2(fd, create.width, create.height,
				  DRM_FORMAT_RGB565, handles, pitches, offsets,
				  &fb_id, 0)) {
			rc = fail("drmModeAddFB2");
			goto out;
		}
	}
	if (strcmp(stage, "fb") == 0) {
		printf("stage=%s complete\n", stage);
		rc = 0;
		goto out;
	}

	resources = drmModeGetResources(fd);
	if (!resources) {
		fprintf(stderr, "drmModeGetResources failed\n");
		goto out;
	}
	if (strcmp(stage, "resources") == 0) {
		printf("stage=%s complete\n", stage);
		rc = 0;
		goto out;
	}
	for (i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *candidate =
			drmModeGetConnector(fd, resources->connectors[i]);
		int j;

		if (!candidate || candidate->connection != DRM_MODE_CONNECTED) {
			if (candidate)
				drmModeFreeConnector(candidate);
			continue;
		}
		for (j = 0; j < candidate->count_modes; j++) {
			if (candidate->modes[j].hdisplay == 1280 &&
			    candidate->modes[j].vdisplay == 720) {
				connector = candidate;
				mode = &connector->modes[j];
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
	if (strcmp(stage, "connector") == 0) {
		printf("stage=%s complete\n", stage);
		rc = 0;
		goto out;
	}
	encoder = drmModeGetEncoder(fd, connector->encoder_id);
	if (!encoder && connector->count_encoders)
		encoder = drmModeGetEncoder(fd, connector->encoders[0]);
	if (!encoder) {
		fprintf(stderr, "no connector encoder\n");
		goto out;
	}
	crtc_id = encoder->crtc_id;
	for (i = 0; !crtc_id && i < resources->count_crtcs; i++)
		if (encoder->possible_crtcs & (1U << i))
			crtc_id = resources->crtcs[i];
	for (i = 0; i < resources->count_crtcs; i++)
		if (resources->crtcs[i] == crtc_id)
			crtc_index = i;
	if (!crtc_id) {
		fprintf(stderr, "no compatible CRTC\n");
		goto out;
	}
	if (strcmp(stage, "encoder-crtc") == 0) {
		printf("stage=%s complete\n", stage);
		rc = 0;
		goto out;
	}
	plane_res = drmModeGetPlaneResources(fd);
	if (!plane_res) {
		fprintf(stderr, "drmModeGetPlaneResources failed\n");
		goto out;
	}
	for (i = 0; i < (int)plane_res->count_planes; i++) {
		drmModePlane *candidate = drmModeGetPlane(fd, plane_res->planes[i]);
		uint32_t type_prop;
		uint64_t type_value;

		if (!candidate || !(candidate->possible_crtcs & (1U << crtc_index)))
			goto next_plane;
		type_prop = get_prop_id(fd, candidate->plane_id,
					DRM_MODE_OBJECT_PLANE, "type");
		if (type_prop && !get_prop_value(fd, candidate->plane_id,
				DRM_MODE_OBJECT_PLANE, type_prop, &type_value) &&
		    type_value == DRM_PLANE_TYPE_PRIMARY) {
			plane = candidate;
			plane_id = candidate->plane_id;
			break;
		}
next_plane:
		if (candidate)
			drmModeFreePlane(candidate);
	}
	if (!plane) {
		fprintf(stderr, "no compatible primary plane\n");
		goto out;
	}
	if (strcmp(stage, "planes") == 0) {
		printf("stage=%s complete\n", stage);
		rc = 0;
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
		fprintf(stderr, "missing atomic properties\n");
		goto out;
	}
	if (strcmp(stage, "properties") == 0) {
		printf("stage=%s complete\n", stage);
		rc = 0;
		goto out;
	}
	if (drmModeCreatePropertyBlob(fd, mode, sizeof(*mode), &mode_blob_id)) {
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
	    drmModeAtomicAddProperty(request, plane_id, props.fb_id, fb_id) < 0 ||
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
	if (strcmp(stage, "atomic-build") == 0) {
		printf("stage=%s complete\n", stage);
		rc = 0;
		goto out;
	}
	if (strcmp(stage, "atomic-test") == 0) {
		if (drmModeAtomicCommit(fd, request,
					DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET,
					NULL)) {
			rc = fail("drmModeAtomicCommit(test)");
			goto out;
		}
		printf("stage=%s complete\n", stage);
		rc = 0;
		goto out;
	}
	if (strcmp(stage, "atomic-commit") == 0) {
		if (drmModeAtomicCommit(fd, request,
					DRM_MODE_ATOMIC_ALLOW_MODESET, NULL)) {
			rc = fail("drmModeAtomicCommit(commit)");
			goto out;
		}
		printf("stage=%s complete\n", stage);
		rc = 0;
		goto out;
	}
	fprintf(stderr, "internal stage error: %s\n", stage);
	goto out;

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
