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

#include <stdio.h>

#include "CUnit/Basic.h"

#include "loonggpu_test.h"
#include "loonggpu_drm.h"

#define BUFFER_SIZE (16*1024)
#define BUFFER_ALIGN (16*1024)

static loonggpu_device_handle device_handle;
static uint32_t major_version;
static uint32_t minor_version;

static loonggpu_bo_handle buffer_handle;
static uint64_t virtual_mc_base_address;
static loonggpu_va_handle va_handle;

static void loonggpu_bo_export_import(void);
static void loonggpu_bo_metadata(void);
static void loonggpu_bo_map_unmap(void);
static void loonggpu_memory_alloc(void);
static void loonggpu_mem_fail_alloc(void);

CU_TestInfo bo_tests[] = {
	{ "Export/Import",  loonggpu_bo_export_import },
	{ "Metadata",  loonggpu_bo_metadata },
	{ "CPU map/unmap",  loonggpu_bo_map_unmap },
	{ "Memory alloc Test",  loonggpu_memory_alloc },
	{ "Memory fail alloc Test",  loonggpu_mem_fail_alloc },
	CU_TEST_INFO_NULL,
};

int suite_bo_tests_init(void)
{
	struct loonggpu_bo_alloc_request req = {0};
	loonggpu_bo_handle buf_handle;
	uint64_t va;
	int r;

	r = loonggpu_device_initialize(drm_loonggpu[0], &major_version,
				  &minor_version, &device_handle);
	if (r) {
		if ((r == -EACCES) && (errno == EACCES))
			printf("\n\nError:%s. "
				"Hint:Try to run this test program as root.",
				strerror(errno));

		return CUE_SINIT_FAILED;
	}

	req.alloc_size = BUFFER_SIZE;
	req.phys_alignment = BUFFER_ALIGN;
	req.preferred_heap = LOONGGPU_GEM_DOMAIN_GTT;

	r = loonggpu_bo_alloc(device_handle, &req, &buf_handle);
	if (r)
		return CUE_SINIT_FAILED;

	r = loonggpu_va_range_alloc(device_handle,
				  loonggpu_gpu_va_range_general,
				  BUFFER_SIZE, BUFFER_ALIGN, 0,
				  &va, &va_handle, 0);
	if (r)
		goto error_va_alloc;

	r = loonggpu_bo_va_op(buf_handle, 0, BUFFER_SIZE, va, 0, LOONGGPU_VA_OP_MAP);
	if (r)
		goto error_va_map;

	buffer_handle = buf_handle;
	virtual_mc_base_address = va;

	return CUE_SUCCESS;

error_va_map:
	loonggpu_va_range_free(va_handle);

error_va_alloc:
	loonggpu_bo_free(buf_handle);
	return CUE_SINIT_FAILED;
}

int suite_bo_tests_clean(void)
{
	int r;

	r = loonggpu_bo_va_op(buffer_handle, 0, BUFFER_SIZE,
			    virtual_mc_base_address, 0,
			    LOONGGPU_VA_OP_UNMAP);
	if (r)
		return CUE_SCLEAN_FAILED;

	r = loonggpu_va_range_free(va_handle);
	if (r)
		return CUE_SCLEAN_FAILED;

	r = loonggpu_bo_free(buffer_handle);
	if (r)
		return CUE_SCLEAN_FAILED;

	r = loonggpu_device_deinitialize(device_handle);
	if (r)
		return CUE_SCLEAN_FAILED;

	return CUE_SUCCESS;
}

static void loonggpu_bo_export_import_do_type(enum loonggpu_bo_handle_type type)
{
	struct loonggpu_bo_import_result res = {0};
	uint32_t shared_handle;
	int r;

	r = loonggpu_bo_export(buffer_handle, type, &shared_handle);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_import(device_handle, type, shared_handle, &res);
	CU_ASSERT_EQUAL(r, 0);

	CU_ASSERT_EQUAL(res.buf_handle, buffer_handle);
	CU_ASSERT_EQUAL(res.alloc_size, BUFFER_SIZE);

	r = loonggpu_bo_free(res.buf_handle);
	CU_ASSERT_EQUAL(r, 0);
}

static void loonggpu_bo_export_import(void)
{
	if (open_render_node) {
		printf("(DRM render node is used. Skip export/Import test) ");
		return;
	}

	loonggpu_bo_export_import_do_type(loonggpu_bo_handle_type_gem_flink_name);
	loonggpu_bo_export_import_do_type(loonggpu_bo_handle_type_dma_buf_fd);
}

static void loonggpu_bo_metadata(void)
{
	struct loonggpu_bo_metadata meta = {0};
	struct loonggpu_bo_info info = {0};
	int r;

	meta.size_metadata = 4;
	meta.umd_metadata[0] = 0xdeadbeef;
	meta.flags= 3 << 9;

	r = loonggpu_bo_set_metadata(buffer_handle, &meta);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_query_info(buffer_handle, &info);
	CU_ASSERT_EQUAL(r, 0);

	CU_ASSERT_EQUAL(info.metadata.size_metadata, 4);
	CU_ASSERT_EQUAL(info.metadata.umd_metadata[0], 0xdeadbeef);
}

static void loonggpu_bo_map_unmap(void)
{
	uint32_t *ptr;
	int i, r;

	r = loonggpu_bo_cpu_map(buffer_handle, (void **)&ptr);
	CU_ASSERT_EQUAL(r, 0);
	CU_ASSERT_NOT_EQUAL(ptr, NULL);

	for (i = 0; i < (BUFFER_SIZE / 4); ++i)
		ptr[i] = 0xdeadbeef;

	r = loonggpu_bo_cpu_unmap(buffer_handle);
	CU_ASSERT_EQUAL(r, 0);
}

static void loonggpu_memory_alloc(void)
{
	loonggpu_bo_handle bo;
	loonggpu_va_handle va_handle;
	uint64_t bo_mc;
	int r;

	/* Test visible VRAM */
	bo = gpu_mem_alloc(device_handle,
			BUFFER_SIZE, BUFFER_ALIGN,
			LOONGGPU_GEM_DOMAIN_VRAM,
			LOONGGPU_GEM_CREATE_CPU_ACCESS_REQUIRED,
			&bo_mc, &va_handle);

	r = gpu_mem_free(bo, va_handle, bo_mc, BUFFER_SIZE);
	CU_ASSERT_EQUAL(r, 0);

	/* Test invisible VRAM */
	bo = gpu_mem_alloc(device_handle,
			BUFFER_SIZE, BUFFER_ALIGN,
			LOONGGPU_GEM_DOMAIN_VRAM,
			LOONGGPU_GEM_CREATE_NO_CPU_ACCESS,
			&bo_mc, &va_handle);

	r = gpu_mem_free(bo, va_handle, bo_mc, BUFFER_SIZE);
	CU_ASSERT_EQUAL(r, 0);

	/* Test GART Cacheable */
	bo = gpu_mem_alloc(device_handle,
			BUFFER_SIZE, BUFFER_ALIGN,
			LOONGGPU_GEM_DOMAIN_GTT,
			0, &bo_mc, &va_handle);

	r = gpu_mem_free(bo, va_handle, bo_mc, BUFFER_SIZE);
	CU_ASSERT_EQUAL(r, 0);

	/* Test GART USWC */
	bo = gpu_mem_alloc(device_handle,
			BUFFER_SIZE, BUFFER_ALIGN,
			LOONGGPU_GEM_DOMAIN_GTT,
			LOONGGPU_GEM_CREATE_CPU_GTT_USWC,
			&bo_mc, &va_handle);

	r = gpu_mem_free(bo, va_handle, bo_mc, BUFFER_SIZE);
	CU_ASSERT_EQUAL(r, 0);
}

static void loonggpu_mem_fail_alloc(void)
{
	loonggpu_bo_handle bo = NULL;
	int r;
	struct loonggpu_bo_alloc_request req = {0};
	loonggpu_bo_handle buf_handle;

	/* Test impossible mem allocation, 1TB */
	req.alloc_size = 0xE8D4A51000;
	req.phys_alignment = BUFFER_ALIGN;
	req.preferred_heap = LOONGGPU_GEM_DOMAIN_VRAM;
	req.flags = LOONGGPU_GEM_CREATE_NO_CPU_ACCESS;

	r = loonggpu_bo_alloc(device_handle, &req, &buf_handle);
	CU_ASSERT_EQUAL(r, -ENOMEM);

	if (!r) {
		r = loonggpu_bo_free(bo);
		CU_ASSERT_EQUAL(r, 0);
	}
}
