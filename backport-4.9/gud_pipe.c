#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "gud_internal.h"

static void gud_fb_destroy(struct drm_framebuffer *fb)
{
	struct gud_framebuffer *gfb = to_gud_framebuffer(fb);

	drm_framebuffer_cleanup(fb);
	drm_gem_object_unreference_unlocked(gfb->obj);
	kfree(gfb);
}

static int gud_fb_create_handle(struct drm_framebuffer *fb,
				struct drm_file *file, unsigned int *handle)
{
	return drm_gem_handle_create(file, to_gud_framebuffer(fb)->obj, handle);
}

static const struct drm_framebuffer_funcs gud_fb_funcs = {
	.destroy = gud_fb_destroy,
	.create_handle = gud_fb_create_handle,
};

static struct drm_framebuffer *gud_fb_create(struct drm_device *dev,
					     struct drm_file *file,
					     const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj;
	struct gud_framebuffer *gfb;
	u64 line_bytes;
	u64 min_size;
	u64 rows_bytes = 0;
	int ret;

	if (!mode_cmd->width || !mode_cmd->height)
		return ERR_PTR(-EINVAL);
	if (!mode_cmd->handles[0])
		return ERR_PTR(-EINVAL);
	if (mode_cmd->pixel_format != DRM_FORMAT_XRGB8888)
		return ERR_PTR(-EINVAL);
	if (mode_cmd->handles[1] || mode_cmd->handles[2] || mode_cmd->handles[3])
		return ERR_PTR(-EINVAL);
	if (mode_cmd->pitches[1] || mode_cmd->pitches[2] || mode_cmd->pitches[3])
		return ERR_PTR(-EINVAL);
	if (mode_cmd->offsets[1] || mode_cmd->offsets[2] || mode_cmd->offsets[3])
		return ERR_PTR(-EINVAL);
	if (mode_cmd->modifier[0] &&
	    mode_cmd->modifier[0] != DRM_FORMAT_MOD_LINEAR)
		return ERR_PTR(-EINVAL);
	if (mode_cmd->modifier[1] || mode_cmd->modifier[2] || mode_cmd->modifier[3])
		return ERR_PTR(-EINVAL);

	line_bytes = (u64)mode_cmd->width * 4;
	if (line_bytes > U32_MAX)
		return ERR_PTR(-EINVAL);
	if (mode_cmd->pitches[0] < line_bytes)
		return ERR_PTR(-EINVAL);
	if (mode_cmd->height > 1) {
		rows_bytes = (u64)(mode_cmd->height - 1) * mode_cmd->pitches[0];
		if (mode_cmd->pitches[0] &&
		    rows_bytes / mode_cmd->pitches[0] != mode_cmd->height - 1)
			return ERR_PTR(-EINVAL);
	}

	obj = drm_gem_object_lookup(file, mode_cmd->handles[0]);
	if (!obj)
		return ERR_PTR(-ENOENT);
	min_size = (u64)mode_cmd->offsets[0] + rows_bytes;
	if (min_size < mode_cmd->offsets[0] || min_size + line_bytes < min_size ||
	    min_size + line_bytes > obj->size) {
		drm_gem_object_unreference_unlocked(obj);
		return ERR_PTR(-EINVAL);
	}

	gfb = kzalloc(sizeof(*gfb), GFP_KERNEL);
	if (!gfb) {
		drm_gem_object_unreference_unlocked(obj);
		return ERR_PTR(-ENOMEM);
	}

	gfb->obj = obj;
	drm_helper_mode_fill_fb_struct(&gfb->base, mode_cmd);

	ret = drm_framebuffer_init(dev, &gfb->base, &gud_fb_funcs);
	if (ret) {
		drm_gem_object_unreference_unlocked(obj);
		kfree(gfb);
		return ERR_PTR(ret);
	}

	return &gfb->base;
}

static int gud_pipe_check(struct drm_simple_display_pipe *pipe,
			  struct drm_plane_state *plane_state,
			  struct drm_crtc_state *crtc_state)
{
	struct gud_device *gud = container_of(pipe, struct gud_device, pipe);

	(void)crtc_state;

	if (gud->disconnected)
		return -ENODEV;
	if (plane_state->fb &&
	    plane_state->fb->pixel_format != DRM_FORMAT_XRGB8888)
		return -EINVAL;
	return 0;
}

static void gud_pipe_enable(struct drm_simple_display_pipe *pipe,
			    struct drm_crtc_state *crtc_state)
{
	(void)pipe;
	(void)crtc_state;
}

static void gud_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	(void)pipe;
}

static void gud_pipe_update(struct drm_simple_display_pipe *pipe,
			    struct drm_plane_state *plane_state)
{
	(void)pipe;
	(void)plane_state;
}

static const u32 gud_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const struct drm_simple_display_pipe_funcs gud_pipe_funcs = {
	.enable = gud_pipe_enable,
	.disable = gud_pipe_disable,
	.check = gud_pipe_check,
	.update = gud_pipe_update,
};

static const struct drm_mode_config_funcs gud_mode_config_funcs = {
	.fb_create = gud_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

int gud_pipe_init(struct gud_device *gud)
{
	int ret;

	drm_mode_config_init(gud->drm);
	gud->drm->mode_config.min_width = 1280;
	gud->drm->mode_config.max_width = 1280;
	gud->drm->mode_config.min_height = 720;
	gud->drm->mode_config.max_height = 720;
	gud->drm->mode_config.funcs = &gud_mode_config_funcs;

	ret = gud_connector_init(gud);
	if (ret)
		goto err_mode_config;

	ret = drm_simple_display_pipe_init(gud->drm, &gud->pipe, &gud_pipe_funcs,
					   gud_formats, ARRAY_SIZE(gud_formats),
					   &gud->connector);
	if (ret)
		goto err_mode_config;

	return 0;

err_mode_config:
	drm_mode_config_cleanup(gud->drm);
	return ret;
}
