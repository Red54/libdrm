/*
 * Copyright (C) 2020 Loongson Technology Co., Ltd.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "loonggpu.h"
#include "loonggpu_drm.h"
#include "xf86drm.h"
#include "loonggpu_internal.h"

int loonggpu_vm_reserve_vmid(loonggpu_device_handle dev, uint32_t flags)
{
	union drm_loonggpu_vm vm;

	vm.in.op = LOONGGPU_VM_OP_RESERVE_VMID;
	vm.in.flags = flags;

	return drmCommandWriteRead(dev->fd, DRM_LOONGGPU_VM,
				   &vm, sizeof(vm));
}

int loonggpu_vm_unreserve_vmid(loonggpu_device_handle dev, uint32_t flags)
{
	union drm_loonggpu_vm vm;

	vm.in.op = LOONGGPU_VM_OP_UNRESERVE_VMID;
	vm.in.flags = flags;

	return drmCommandWriteRead(dev->fd, DRM_LOONGGPU_VM,
				   &vm, sizeof(vm));
}
