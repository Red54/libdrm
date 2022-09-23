/*
 * Copyright (C) 2020 Loongson Technology Co., Ltd.
 * All Rights Reserved.
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

#include "libdrm_macros.h"
#include "xf86drm.h"
#include "gsgpu_drm.h"
#include "gsgpu_internal.h"

int gsgpu_hw_sema_get(gsgpu_device_handle dev, gsgpu_context_handle ctx, uint64_t *sema)
{
	int r = 0;
	struct drm_gsgpu_hw_sema args = {0};

	args.ctx_id = ctx->id;
	args.ops = GSGPU_HW_SEMA_GET;

	r = drmCommandWriteRead(dev->fd, DRM_GSGPU_HWSEMA_OP,
				&args, sizeof(args));
	if (!r)
		*sema = args.id;

	return r;
}

int gsgpu_hw_sema_put(gsgpu_device_handle dev, gsgpu_context_handle ctx, uint64_t sema)
{
	int r = 0;

	struct drm_gsgpu_hw_sema args = {0};

	args.id = sema;
	args.ctx_id = ctx->id;
	args.ops = GSGPU_HW_SEMA_PUT;

	r = drmCommandWriteRead(dev->fd, DRM_GSGPU_HWSEMA_OP,
				&args, sizeof(args));

	return r;
}
