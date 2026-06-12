/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>

#include "xf86drm.h"
#include "amdgpu_drm.h"
#include "amdgpu_internal.h"

static int amdgpu_svm_check_params(uint64_t start_addr, uint64_t size)
{
	long page_size = sysconf(_SC_PAGESIZE);

	if (page_size <= 0)
		return -EINVAL;
	if (!start_addr || !size)
		return -EINVAL;
	if (start_addr & (page_size - 1))
		return -EINVAL;
	if (size & (page_size - 1))
		return -EINVAL;

	return 0;
}

drm_public int amdgpu_svm_set_attr(amdgpu_device_handle dev,
				   uint64_t start_addr,
				   uint64_t size, uint32_t nattr,
				   struct drm_amdgpu_svm_attribute *attrs)
{
	struct drm_amdgpu_gem_svm args = {0};
	int r;

	r = amdgpu_svm_check_params(start_addr, size);
	if (r)
		return r;

	args.start_addr = start_addr;
	args.size = size;
	args.operation = AMDGPU_SVM_OP_SET_ATTR;
	args.nattr = nattr;
	args.attrs_ptr = (uint64_t)(uintptr_t)attrs;

	return drmCommandWriteRead(dev->fd, DRM_AMDGPU_GEM_SVM,
				  &args, sizeof(args));
}

drm_public int amdgpu_svm_get_attr(amdgpu_device_handle dev,
				   uint64_t start_addr,
				   uint64_t size, uint32_t nattr,
				   struct drm_amdgpu_svm_attribute *attrs)
{
	struct drm_amdgpu_gem_svm args = {0};
	int r;
	uint32_t i;

	r = amdgpu_svm_check_params(start_addr, size);
	if (r)
		return r;

	args.start_addr = start_addr;
	args.size = size;
	args.operation = AMDGPU_SVM_OP_GET_ATTR;
	args.nattr = nattr;
	args.attrs_ptr = (uint64_t)(uintptr_t)attrs;

	r = drmCommandWriteRead(dev->fd, DRM_AMDGPU_GEM_SVM,
				&args, sizeof(args));
	if (r)
		return r;

	/*
	 * Post-process location values:
	 *   SYSMEM    (0)          → 0
	 *   UNDEFINED (0xffffffff) → 0xffffffff
	 *   GPU fd    (other)      → pass through as-is
	 */
	for (i = 0; i < nattr; i++) {
		if (attrs[i].type != AMDGPU_SVM_ATTR_PREFERRED_LOC &&
		    attrs[i].type != AMDGPU_SVM_ATTR_PREFETCH_LOC)
			continue;

		switch (attrs[i].value) {
		case AMDGPU_SVM_LOCATION_SYSMEM:
			attrs[i].value = 0;
			break;
		case AMDGPU_SVM_LOCATION_UNDEFINED:
			attrs[i].value = AMDGPU_SVM_LOCATION_UNDEFINED;
			break;
		default:
			/* GPU id – leave as-is */
			break;
		}
	}

	return 0;
}

drm_public int amdgpu_svm_reset_attr(amdgpu_device_handle dev,
				   uint64_t start_addr,
				   uint64_t size)
{
	struct drm_amdgpu_gem_svm args = {0};
	int r;

	r = amdgpu_svm_check_params(start_addr, size);
	if (r)
		return r;

	args.start_addr = start_addr;
	args.size = size;
	args.operation = AMDGPU_SVM_OP_RESET_ATTR;
	args.nattr = 0;
	args.attrs_ptr = 0;

	return drmCommandWriteRead(dev->fd, DRM_AMDGPU_GEM_SVM,
				   &args, sizeof(args));
}
