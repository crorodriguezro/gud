#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "gud_internal.h"

struct gud_gem_object *gud_gem_create(struct drm_device *dev, size_t size)
{
	struct gud_gem_object *obj;
	int ret;

	if (!size || size > SIZE_MAX - (PAGE_SIZE - 1))
		return ERR_PTR(-EINVAL);

	size = ALIGN(size, PAGE_SIZE);
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	ret = drm_gem_object_init(dev, &obj->base, size);
	if (ret) {
		kfree(obj);
		return ERR_PTR(ret);
	}

	return obj;
}

int gud_gem_get_pages(struct gud_gem_object *obj)
{
	if (obj->base.import_attach)
		return -EOPNOTSUPP;
	if (obj->pages)
		return 0;

	obj->pages = drm_gem_get_pages(&obj->base);
	if (IS_ERR(obj->pages)) {
		int ret = PTR_ERR(obj->pages);

		obj->pages = NULL;
		return ret;
	}

	return 0;
}

int gud_gem_vmap(struct gud_gem_object *obj, void **vaddr, size_t *size)
{
	int ret;

	if (obj->base.import_attach)
		return -EOPNOTSUPP;

	ret = gud_gem_get_pages(obj);
	if (ret)
		return ret;

	if (!obj->vaddr) {
		obj->vaddr = vmap(obj->pages, obj->base.size >> PAGE_SHIFT, 0,
				  PAGE_KERNEL);
		if (!obj->vaddr)
			return -ENOMEM;
	}

	*vaddr = obj->vaddr;
	*size = obj->base.size;

	return 0;
}

void gud_gem_free_object(struct drm_gem_object *gem)
{
	struct gud_gem_object *obj = to_gud_gem(gem);

	if (obj->vaddr)
		vunmap(obj->vaddr);
	if (obj->pages)
		drm_gem_put_pages(&obj->base, obj->pages, false, false);
	drm_gem_object_release(gem);
	kfree(obj);
}

	int gud_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
			struct drm_mode_create_dumb *args)
{
	struct gud_gem_object *obj;
	size_t pitch, size;
	unsigned int bytes_per_pixel;
	int ret;

	if (args->bpp != 16 && args->bpp != 32)
		return -EINVAL;
	if (!args->width || !args->height)
		return -EINVAL;
	bytes_per_pixel = args->bpp / 8;
	if (args->width > U32_MAX / bytes_per_pixel)
		return -EINVAL;

	pitch = ALIGN((size_t)args->width * bytes_per_pixel, 4);
	if (args->height > SIZE_MAX / pitch)
		return -EINVAL;

	size = pitch * args->height;

	obj = gud_gem_create(dev, size);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	ret = drm_gem_handle_create(file, &obj->base, &args->handle);
	if (ret) {
		gud_gem_free_object(&obj->base);
		return ret;
	}

	drm_gem_object_unreference_unlocked(&obj->base);
	args->pitch = pitch;
	args->size = obj->base.size;

	return 0;
}

int gud_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
			    u32 handle, u64 *offset)
{
	struct drm_gem_object *obj;
	int ret;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;
	if (obj->import_attach) {
		ret = -EOPNOTSUPP;
		goto out_unref;
	}

	ret = gud_gem_get_pages(to_gud_gem(obj));
	if (ret)
		goto out_unref;

	ret = drm_gem_create_mmap_offset(obj);
	if (!ret)
		*offset = drm_vma_node_offset_addr(&obj->vma_node);

out_unref:
	drm_gem_object_unreference_unlocked(obj);
	return ret;
}

static int gud_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct gud_gem_object *obj = to_gud_gem(vma->vm_private_data);
	unsigned long page_offset;
	struct page *page;
	int ret;

	page_offset = ((unsigned long)vmf->virtual_address - vma->vm_start) >>
		PAGE_SHIFT;
	if (!obj->pages || page_offset >= obj->base.size >> PAGE_SHIFT)
		return VM_FAULT_SIGBUS;

	page = obj->pages[page_offset];
	ret = vm_insert_page(vma, (unsigned long)vmf->virtual_address, page);
	switch (ret) {
	case 0:
	case -EAGAIN:
	case -ERESTARTSYS:
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	default:
		return VM_FAULT_SIGBUS;
	}
}

const struct vm_operations_struct gud_gem_vm_ops = {
	.fault = gud_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

int gud_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;

	return 0;
}
