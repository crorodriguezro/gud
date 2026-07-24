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
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

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
	struct object_props props = { 0 };
	void *pixels = MAP_FAILED;
	drmModeModeInfo *mode = NULL;
	uint32_t mode_blob_id = 0;
	uint32_t fb_id = 0;
	uint32_t connector_id = 0;
	uint32_t crtc_id = 0;
	uint32_t plane_id = 0;
	int connector_index;
	uint32_t crtc_index = 0;
	int fd = -1;
	int rc = 1;
	int i;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <card-path>\n", argv[0]);
		return 1;
	}
	card = argv[1];

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

	create.width = 1280;
	create.height = 720;
	create.bpp = 32;
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

	{
		uint32_t handles[4] = { create.handle, 0, 0, 0 };
		uint32_t pitches[4] = { create.pitch, 0, 0, 0 };
		uint32_t offsets[4] = { 0, 0, 0, 0 };

		if (drmModeAddFB2(fd, create.width, create.height, DRM_FORMAT_XRGB8888,
				  handles, pitches, offsets, &fb_id, 0) != 0) {
			rc = fail("drmModeAddFB2");
			goto out;
		}
	}

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

	if (drmModeAtomicCommit(fd, request, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL) != 0) {
		rc = fail("drmModeAtomicCommit");
		goto out;
	}

	printf("card=%s connector=%" PRIu32 " crtc=%" PRIu32 " plane=%" PRIu32
	       " dumb_handle=%" PRIu32 " fb_id=%" PRIu32 "\n",
	       card, connector_id, crtc_id, plane_id, create.handle, fb_id);
	printf("atomic modeset succeeded\n");
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
