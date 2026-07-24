#ifndef __GUD_COMPAT_4_9_H__
#define __GUD_COMPAT_4_9_H__

#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_simple_kms_helper.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR DRM_FORMAT_MOD_NONE
#endif

#endif
