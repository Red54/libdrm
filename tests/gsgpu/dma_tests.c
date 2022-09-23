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
#include <stdlib.h>
#include <unistd.h>

#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#endif

#include <sys/wait.h>

#include <stdio.h>

#include "CUnit/Basic.h"

#include "gsgpu_test.h"
#include "gsgpu_drm.h"

extern  gsgpu_device_handle device_handle;
extern  uint32_t  major_version;
extern  uint32_t  minor_version;
extern  uint32_t  family_id;

#define TABLE_SIZE(x)   (sizeof(x)/sizeof(x[0]))
#define BUFFER_SIZE (16 * 1024)
#define BUFFER_ALIGN (16 * 1024)
#define MSAA_PIXEL (2*2)
#define MINIMUM 0
#define EQUAL_LENGTH 1
#define UNEQUAL_LENGTH 2

union pixel_rgba8 {
        uint32_t dw;
        struct {
                uint8_t a;
                uint8_t b;
                uint8_t g;
                uint8_t r;
        }channel;
};

struct gsgpu_xdma_cmd_desc {
	union {
		uint32_t val;
		struct {
			uint32_t opcode : 8;
			uint32_t format : 8;
			uint32_t length : 8;
			uint32_t opmode : 4;
			uint32_t sub_mode : 4;
		}sec;
	}header;
	struct {
		union {
			uint32_t data_size;
			struct {
				uint16_t width;
				uint16_t height;
			}size;
		};
		uint32_t src_lo;
		uint32_t src_hi;
		uint32_t dst_lo;
		uint32_t dst_hi;
		uint32_t src_stride;
		uint32_t dst_stride;
		union {
			uint32_t val;
			struct {
				uint32_t    : 12;
				uint32_t rd : 5;
				uint32_t rd_en : 1;
				uint32_t    : 2;
				uint32_t wr : 5;
				uint32_t wr_en : 1;
				uint32_t    : 0;
			};
		}sema;

	}body;
};

static void generate_once_mipmap(union pixel_rgba8 * const src, union pixel_rgba8 * const dst, const uint16_t width, const uint16_t height);
static int verify_msaa_resolve(union pixel_rgba8 *src, union pixel_rgba8 *dst);
static void verify_mipmaps(union pixel_rgba8 * const src, union pixel_rgba8 * const dst, const uint16_t width, const uint16_t height);
static void *acquire_semaphore_thread_entry(void *number);
static void *release_semaphore_thread_entry(void *number);
static uint32_t get_pixel_depth(uint32_t format);
static union pixel_rgba8 *get_tile_pixel(union pixel_rgba8 * const base, const int x, const int y, const int pitch);

extern void gsgpu_command_submission_write_linear_helper(unsigned ip_type);
static void gsgpu_command_submission_const_fill_helper(unsigned ip_type);
static void gsgpu_command_submission_copy_linear_helper(unsigned ip_type);
static void gsgpu_command_submission_copy_tiled_helper(unsigned ip_type);
static void gsgpu_command_submission_msaa_resolve_helper(unsigned ip_type);
static void gsgpu_command_submission_mipmap_generate_helper(unsigned ip_type, uint16_t width, uint16_t height);
static void gsgpu_command_submission_sdma_semaphore_helper(unsigned ip_type);
extern void gsgpu_test_exec_cs_helper(gsgpu_context_handle context_handle,
                                       unsigned ip_type,
                                       int instance, int pm4_dw, uint32_t *pm4_src,
                                       int res_cnt, gsgpu_bo_handle *resources,
                                       struct gsgpu_cs_ib_info *ib_info,
                                       struct gsgpu_cs_request *ibs_request);

static void gsgpu_command_submission_sdma_write_linear(void);
static void gsgpu_command_submission_sdma_const_fill(void);
static void gsgpu_command_submission_sdma_copy_linear(void);
static void gsgpu_command_submission_sdma_copy_tiled(void);
static void gsgpu_command_submission_sdma_msaa_resolve(void);
static void gsgpu_command_submission_sdma_mipmap_generate(void);
static void gsgpu_command_submission_sdma_semaphore(void);

CU_TestInfo dma_tests[] = {
	{ "Command submission Test (DMA write)", gsgpu_command_submission_sdma_write_linear },
	{ "Command submission Test (DMA fill)", gsgpu_command_submission_sdma_const_fill },
	{ "Command submission Test (DMA copy linear)", gsgpu_command_submission_sdma_copy_linear },
	{ "Command submission Test (DMA copy tiled)", gsgpu_command_submission_sdma_copy_tiled },
	{ "Command submission Test (DMA copy msaa)", gsgpu_command_submission_sdma_msaa_resolve },
	{ "Command submission Test (DMA copy mipmap)", gsgpu_command_submission_sdma_mipmap_generate },
	{ "Command submission Test (semaphore)", gsgpu_command_submission_sdma_semaphore },
	CU_TEST_INFO_NULL,
};

int suite_dma_tests_init(void)
{
	struct gsgpu_gpu_info gpu_info = {0};
	int r;

	r = gsgpu_device_initialize(drm_gsgpu[0], &major_version,
				   &minor_version, &device_handle);

	if (r) {
		if ((r == -EACCES) && (errno == EACCES))
			printf("\n\nError:%s. "
				"Hint:Try to run this test program as root.",
				strerror(errno));
		return CUE_SINIT_FAILED;
	}

	r = gsgpu_query_gpu_info(device_handle, &gpu_info);
	if (r)
		return CUE_SINIT_FAILED;

	family_id = gpu_info.family_id;

	return CUE_SUCCESS;
}

int suite_dma_tests_clean(void)
{
	int r = gsgpu_device_deinitialize(device_handle);

	if (r == 0)
		return CUE_SUCCESS;
	else
		return CUE_SCLEAN_FAILED;
}

static void gsgpu_command_submission_sdma_write_linear(void)
{
	gsgpu_command_submission_write_linear_helper(GSGPU_HW_IP_DMA);
}

static void gsgpu_command_submission_sdma_const_fill(void)
{
	gsgpu_command_submission_const_fill_helper(GSGPU_HW_IP_DMA);
}

static void gsgpu_command_submission_const_fill_helper(unsigned ip_type)
{
	const int pm4_dw = 256;
	gsgpu_context_handle context_handle;
	gsgpu_bo_handle bo;
	gsgpu_bo_handle *resources;
	uint32_t *pm4;
	struct gsgpu_cs_ib_info *ib_info;
	struct gsgpu_cs_request *ibs_request;
	uint64_t bo_mc;
	volatile uint32_t *bo_cpu;
	int i, j, r, align_dw;
	uint64_t gtt_flags[1] = {0};
	gsgpu_va_handle va_handle;
	struct drm_gsgpu_info_hw_ip hw_ip_info;

	/* init object of command package */
	struct gsgpu_xdma_cmd_desc cmd_buffer[] = {
		{
			.header.sec = {
				GSGPU_CMD_XDMA_COPY,
				GSGPU_CMD_XDMA_FORMAT_RGBA8,
				GSGPU_CMD_XDMA_BODY_NR,
				GSGPU_CMD_XDMA_MODE_MEMSET,
				GSGPU_CMD_XDMA_SUB_MODE_DEFAULT,
			},
			.body = {
				.size.width = 16 * 1024,
				.size.height = 1,
				.src_lo = 0xdeadbeaf,
				.src_hi = 0,
				.dst_lo = 0,
				.dst_hi = 0,
				.src_stride = 0,
				.dst_stride = 16 * 1024,
				.sema.val = 0,
			},
		},

	};

	pm4 = calloc(pm4_dw, sizeof(*pm4));
	CU_ASSERT_NOT_EQUAL(pm4, NULL);

	ib_info = calloc(1, sizeof(*ib_info));
	CU_ASSERT_NOT_EQUAL(ib_info, NULL);

	ibs_request = calloc(1, sizeof(*ibs_request));
	CU_ASSERT_NOT_EQUAL(ibs_request, NULL);

	r = gsgpu_query_hw_ip_info(device_handle, ip_type, 0, &hw_ip_info);
	CU_ASSERT_EQUAL(r, 0);

	r = gsgpu_cs_ctx_create(device_handle, &context_handle);
	CU_ASSERT_EQUAL(r, 0);

	/* prepare resource */
	resources = calloc(1, sizeof(gsgpu_bo_handle));
	CU_ASSERT_NOT_EQUAL(resources, NULL);

	for (int index = 0; index < TABLE_SIZE(cmd_buffer); index++) {
		int sdma_write_length = get_pixel_depth(cmd_buffer[index].header.sec.format) * cmd_buffer[index].body.size.width * cmd_buffer[index].body.size.height;

		/* allocate bo for DMA use */
		r = gsgpu_bo_alloc_and_map(device_handle,
					    sdma_write_length, BUFFER_ALIGN,
					    GSGPU_GEM_DOMAIN_GTT,
					    0, &bo,
					    (void**)&bo_cpu, &bo_mc,
					    &va_handle);
		CU_ASSERT_EQUAL(r, 0);

		/* clear bo */
		memset((void*)bo_cpu, 0, sdma_write_length);

		resources[0] = bo;

		/* fulfill cmd packet: test SDMA const fill */
		cmd_buffer[index].body.dst_lo = (uint32_t)(0xffffffff & bo_mc);
		cmd_buffer[index].body.dst_hi = (uint32_t)((0xffffffff00000000 & bo_mc) >> 32);

		i = sizeof(cmd_buffer[index]);
		memcpy(pm4, &cmd_buffer[index], i);

		i = (cmd_buffer[index].header.sec.length + 1);

		/* ib cmd packet align */
		align_dw = hw_ip_info.ib_size_alignment - (i & (hw_ip_info.ib_size_alignment - 1));
		for (j = 0; j < align_dw; j++) {
			pm4[i++] = GSGPU_CMD_NOP;
		}

		gsgpu_test_exec_cs_helper(context_handle,
					   ip_type, 0,
					   i, pm4,
					   1, resources,
					   ib_info, ibs_request);

		/* verify if SDMA test result meets with expected */
		i = 0;
		while (i < sdma_write_length / get_pixel_depth(cmd_buffer[index].header.sec.format)) {
			CU_ASSERT_EQUAL(bo_cpu[i++], 0xdeadbeaf);
		}

		r = gsgpu_bo_unmap_and_free(bo, va_handle, bo_mc,
					     sdma_write_length);
		CU_ASSERT_EQUAL(r, 0);
	}

	/* clean resources */
	free(resources);
	free(ibs_request);
	free(ib_info);
	free(pm4);

	/* end of test */
	r = gsgpu_cs_ctx_free(context_handle);
	CU_ASSERT_EQUAL(r, 0);
}


static void gsgpu_command_submission_sdma_copy_linear(void)
{
	gsgpu_command_submission_copy_linear_helper(GSGPU_HW_IP_DMA);
}

static void gsgpu_command_submission_copy_linear_helper(unsigned ip_type)
{
	const int pm4_dw = 256;
	gsgpu_context_handle context_handle = NULL;
	gsgpu_bo_handle bo1 = NULL, bo2 = NULL;
	gsgpu_bo_handle *resources;
	uint32_t *pm4;
	struct gsgpu_cs_ib_info *ib_info;
	struct gsgpu_cs_request *ibs_request;
	uint64_t bo1_mc, bo2_mc;
	volatile unsigned char *bo1_cpu, *bo2_cpu;
	int i, j, r, ring_id, align_dw;
	uint64_t gtt_flags[1] = {0};
	gsgpu_va_handle bo1_va_handle = NULL, bo2_va_handle = NULL;
	struct drm_gsgpu_info_hw_ip hw_ip_info;

	/* init object of command package */
	struct gsgpu_xdma_cmd_desc cmd_buffer[] = {
		{
			.header.sec = {
				GSGPU_CMD_XDMA_COPY,
				GSGPU_CMD_XDMA_FORMAT_RGBA16,
				GSGPU_CMD_XDMA_BODY_NR,
				GSGPU_CMD_XDMA_MODE_L2L,
				GSGPU_CMD_XDMA_SUB_MODE_DEFAULT,
			},
			.body = {
				.size.width = 1024 / 8,
				.size.height = 1,
				.src_lo = 0,
				.src_hi = 0,
				.dst_lo = 0,
				.dst_hi = 0,
				.src_stride = 1024,
				.dst_stride = 1024,
				.sema.rd = 0,
				.sema.rd_en = 0,
				.sema.wr = 0,
				.sema.wr_en = 0,
			},
		},
		{
			.header.sec = {
				GSGPU_CMD_XDMA_COPY,
				GSGPU_CMD_XDMA_FORMAT_RGBA8,
				GSGPU_CMD_XDMA_BODY_NR,
				GSGPU_CMD_XDMA_MODE_L2L,
				GSGPU_CMD_XDMA_SUB_MODE_DEFAULT,
			},
			.body = {
				.size.width = 128,
				.size.height = 1,
				.src_lo = 0,
				.src_hi = 0,
				.dst_lo = 0,
				.dst_hi = 0,
				.src_stride = 128 * 4,
				.dst_stride = 128 * 4,
				.sema.rd = 0,
				.sema.rd_en = 0,
				.sema.wr = 0,
				.sema.wr_en = 0,
			},
		},
	};

	pm4 = calloc(pm4_dw, sizeof(*pm4));
	CU_ASSERT_NOT_EQUAL(pm4, NULL);

	ib_info = calloc(1, sizeof(*ib_info));
	CU_ASSERT_NOT_EQUAL(ib_info, NULL);

	ibs_request = calloc(1, sizeof(*ibs_request));
	CU_ASSERT_NOT_EQUAL(ibs_request, NULL);

	r = gsgpu_query_hw_ip_info(device_handle, ip_type, 0, &hw_ip_info);
	CU_ASSERT_EQUAL(r, 0);

	r = gsgpu_cs_ctx_create(device_handle, &context_handle);
	CU_ASSERT_EQUAL(r, 0);

	/* prepare resource */
	resources = calloc(2, sizeof(gsgpu_bo_handle));
	CU_ASSERT_NOT_EQUAL(resources, NULL);

	for (int index = 0; index < TABLE_SIZE(cmd_buffer); index++) {
		int sdma_write_length = get_pixel_depth(cmd_buffer[index].header.sec.format) * cmd_buffer[index].body.size.width * cmd_buffer[index].body.size.height;
		/* allocate UC bo1for sDMA use */
		r = gsgpu_bo_alloc_and_map(device_handle,
					    sdma_write_length, 4096,
					    GSGPU_GEM_DOMAIN_GTT,
					    gtt_flags[0], &bo1,
					    (void**)&bo1_cpu, &bo1_mc,
					    &bo1_va_handle);
		CU_ASSERT_EQUAL(r, 0);

		/* set bo1 */
		memset((void*)bo1_cpu, 0xaa, sdma_write_length);

		/* allocate UC bo2 for sDMA use */
		r = gsgpu_bo_alloc_and_map(device_handle,
					    sdma_write_length, 4096,
					    GSGPU_GEM_DOMAIN_GTT,
					    gtt_flags[0], &bo2,
					    (void**)&bo2_cpu, &bo2_mc,
					    &bo2_va_handle);
		CU_ASSERT_EQUAL(r, 0);

		/* clear bo2 */
		memset((void*)bo2_cpu, 0, sdma_write_length);

		resources[0] = bo1;
		resources[1] = bo2;

		/* fulfill PM4: test DMA copy linear */
		cmd_buffer[index].body.src_lo = (uint32_t)(0xffffffff & bo1_mc);
		cmd_buffer[index].body.src_hi = (uint32_t)((0xffffffff00000000 & bo1_mc) >> 32);
		cmd_buffer[index].body.dst_lo = (uint32_t)(0xffffffff & bo2_mc);
		cmd_buffer[index].body.dst_hi = (uint32_t)((0xffffffff00000000 & bo2_mc) >> 32);

		i = sizeof(cmd_buffer[index]);
		memcpy(pm4, &cmd_buffer[index], i);

		i = (cmd_buffer[index].header.sec.length + 1);

		/* ib cmd packet align */
		align_dw = hw_ip_info.ib_size_alignment - (i & (hw_ip_info.ib_size_alignment - 1));
		for (j = 0; j < align_dw; j++) {
			pm4[i++] = GSGPU_CMD_NOP;
		}

		gsgpu_test_exec_cs_helper(context_handle,
					   ip_type, 0,
					   i, pm4,
					   2, resources,
					   ib_info, ibs_request);

		/* verify if SDMA test result meets with expected */
		i = 0;
		while(i < sdma_write_length) {
			CU_ASSERT_EQUAL(bo2_cpu[i++], 0xaa);
		}
		r = gsgpu_bo_unmap_and_free(bo1, bo1_va_handle, bo1_mc,
					     sdma_write_length);
		CU_ASSERT_EQUAL(r, 0);
		r = gsgpu_bo_unmap_and_free(bo2, bo2_va_handle, bo2_mc,
					     sdma_write_length);
		CU_ASSERT_EQUAL(r, 0);
	}
	/* clean resources */
	free(resources);
	free(ibs_request);
	free(ib_info);
	free(pm4);

	/* end of test */
	r = gsgpu_cs_ctx_free(context_handle);
	CU_ASSERT_EQUAL(r, 0);
}

static int verify_msaa_resolve(union pixel_rgba8 *src, union pixel_rgba8 *dst)
{
        union pixel_rgba8 ret = {1};
        uint32_t value_rgba[4] = {0, 0, 0, 0};
        for (int index = 0; index < MSAA_PIXEL; index++) {
                value_rgba[0] += (uint32_t)(*(src + index)).channel.r;
                value_rgba[1] += (uint32_t)(*(src + index)).channel.g;
                value_rgba[2] += (uint32_t)(*(src + index)).channel.b;
                value_rgba[3] += (uint32_t)(*(src + index)).channel.a;
        }
        ret.channel.r = (uint8_t)(value_rgba[0]/(MSAA_PIXEL));
        ret.channel.g = (uint8_t)(value_rgba[1]/(MSAA_PIXEL));
        ret.channel.b = (uint8_t)(value_rgba[2]/(MSAA_PIXEL));
        ret.channel.a = (uint8_t)(value_rgba[3]/(MSAA_PIXEL));

        if (ret.dw != (*dst).dw) return 1;
        return 0;
}

static void gsgpu_command_submission_msaa_resolve_helper(unsigned ip_type)
{
        const int pm4_dw = 256;
        gsgpu_context_handle context_handle;
        gsgpu_bo_handle bo1, bo2;
        gsgpu_bo_handle *resources;
        uint32_t *pm4;
        struct gsgpu_cs_ib_info *ib_info;
        struct gsgpu_cs_request *ibs_request;
        uint64_t bo1_mc, bo2_mc;
        volatile union pixel_rgba8 *bo1_cpu, *bo2_cpu;
        int i, j, r, loop1, loop2, ring_id, align_dw;
        uint64_t gtt_flags[2] = {0};
        gsgpu_va_handle bo1_va_handle, bo2_va_handle;
        struct drm_gsgpu_info_hw_ip hw_ip_info;

	/* init object of command package */
	struct gsgpu_xdma_cmd_desc cmd_buffer[] = {
		{
			.header.sec = {
				GSGPU_CMD_XDMA_COPY,
				GSGPU_CMD_XDMA_FORMAT_RGBA8,
				GSGPU_CMD_XDMA_BODY_NR,
				GSGPU_CMD_XDMA_MODE_MSAA,
				GSGPU_CMD_XDMA_SUB_MODE_TILED_4X4,
			},
			.body = {
				.size.width = 8,
				.size.height = 8,
				.src_lo = 0,
				.src_hi = 0,
				.dst_lo = 0,
				.dst_hi = 0,
				.src_stride = 8 * 4 * 4 * 4,
				.dst_stride = 8 * 4 * 4,
				.sema.rd = 0,
				.sema.rd_en = 0,
				.sema.wr = 0,
				.sema.wr_en = 0,
			},
		},
	};

        pm4 = calloc(pm4_dw, sizeof(*pm4));
        CU_ASSERT_NOT_EQUAL(pm4, NULL);

        ib_info = calloc(1, sizeof(*ib_info));
        CU_ASSERT_NOT_EQUAL(ib_info, NULL);

        ibs_request = calloc(1, sizeof(*ibs_request));
        CU_ASSERT_NOT_EQUAL(ibs_request, NULL);

        r = gsgpu_query_hw_ip_info(device_handle, ip_type, 0, &hw_ip_info);
        CU_ASSERT_EQUAL(r, 0);

        r = gsgpu_cs_ctx_create(device_handle, &context_handle);
        CU_ASSERT_EQUAL(r, 0);

        /* prepare resource */
        resources = calloc(2, sizeof(gsgpu_bo_handle));
        CU_ASSERT_NOT_EQUAL(resources, NULL);
	for (int index = 0; index < TABLE_SIZE(cmd_buffer); index++) {
		int sdma_write_length = get_pixel_depth(cmd_buffer[index].header.sec.format) * cmd_buffer[index].body.size.width * cmd_buffer[index].body.size.height;
		/* allocate UC bo1for sDMA use */
		r = gsgpu_bo_alloc_and_map(device_handle,
					    sdma_write_length, 4096,
					    GSGPU_GEM_DOMAIN_GTT,
					    gtt_flags[loop1], &bo1,
					    (void**)&bo1_cpu, &bo1_mc,
					    &bo1_va_handle);
		CU_ASSERT_EQUAL(r, 0);

		/* set bo1 */
		for (int i = 0; i < sdma_write_length/4; i++) {
			(*(bo1_cpu + i)).dw = rand() & 0xffffffff;
		}

		/* allocate UC bo2 for sDMA use */
		r = gsgpu_bo_alloc_and_map(device_handle,
					    sdma_write_length, 4096,
					    GSGPU_GEM_DOMAIN_GTT,
					    gtt_flags[loop2], &bo2,
					    (void**)&bo2_cpu, &bo2_mc,
					    &bo2_va_handle);
		CU_ASSERT_EQUAL(r, 0);

		/* clear bo2 */
		memset((void*)bo2_cpu, 0, sdma_write_length);

		resources[0] = bo1;
		resources[1] = bo2;

		/* fulfill PM4: test DMA msaa reslove */
		cmd_buffer[index].body.src_lo = (uint32_t)(0xffffffff & bo1_mc);
		cmd_buffer[index].body.src_hi = (uint32_t)((0xffffffff00000000 & bo1_mc) >> 32);
		cmd_buffer[index].body.dst_lo = (uint32_t)(0xffffffff & bo2_mc);
		cmd_buffer[index].body.dst_hi = (uint32_t)((0xffffffff00000000 & bo2_mc) >> 32);

		i = sizeof(cmd_buffer[index]);
		memcpy(pm4, &cmd_buffer[index], i);

		i = (cmd_buffer[index].header.sec.length + 1);

		/* ib cmd packet align */
		align_dw = hw_ip_info.ib_size_alignment - (i & (hw_ip_info.ib_size_alignment - 1));
		for (j = 0; j < align_dw; j++) {
			pm4[i++] = GSGPU_CMD_NOP;
		}

		gsgpu_test_exec_cs_helper(context_handle,
					   ip_type, ring_id,
					   i, pm4,
					   2, resources,
					   ib_info, ibs_request);

		/* verify if SDMA test result meets with expected */
		for (int i = 0; i < sdma_write_length/4; i += MSAA_PIXEL) {
			int ret = verify_msaa_resolve((union pixel_rgba8 *)(bo1_cpu + i), (union pixel_rgba8 *)(bo2_cpu + i/MSAA_PIXEL));
			CU_ASSERT_EQUAL(ret, 0);
		}
		r = gsgpu_bo_unmap_and_free(bo1, bo1_va_handle, bo1_mc,
					     sdma_write_length);
		CU_ASSERT_EQUAL(r, 0);
		r = gsgpu_bo_unmap_and_free(bo2, bo2_va_handle, bo2_mc,
					     sdma_write_length);
		CU_ASSERT_EQUAL(r, 0);
	}

        /* clean resources */
        free(resources);
        free(ibs_request);
        free(ib_info);
        free(pm4);

        /* end of test */
        r = gsgpu_cs_ctx_free(context_handle);
        CU_ASSERT_EQUAL(r, 0);
}

static void gsgpu_command_submission_sdma_msaa_resolve(void)
{
        gsgpu_command_submission_msaa_resolve_helper(GSGPU_HW_IP_DMA);
}


union pixel_rgba8 *get_tile_pixel(union pixel_rgba8 * const base, const int x, const int y, const int pitch)
{
	union pixel_rgba8 * tile_base = base;
	//for title only
	int tile_x = x >> 2;
	int tile_y = y >> 2;

	tile_base = base + (tile_y * pitch * 4 + tile_x * 16);

	int tile_offset = ( ((y>>1)&1) ? 0x8 : 0 )
			| ( ((x>>1)&1) ? 0x4 : 0 )
			| ( ((y>>0)&1) ? 0x2 : 0 )
			| ( ((x>>0)&1) ? 0x1 : 0 );

	return &tile_base[tile_offset];
}

static void verify_mipmaps(union pixel_rgba8 * const src, union pixel_rgba8 * const dst, const uint16_t width, const uint16_t height)
{
	union pixel_rgba8 *rptr = src;
	union pixel_rgba8 *wptr = src;
	union pixel_rgba8 *mip_test;
	union pixel_rgba8 *mip_dma;

	generate_once_mipmap(rptr, wptr, width, height);

	for (int y = 0; y < height; y++){
		for (int x = 0; x < width; x++){
			mip_test = get_tile_pixel(src, x, y, width);
			mip_dma = get_tile_pixel(dst, x, y, width);
			CU_ASSERT_EQUAL((*mip_test).dw, (*mip_dma).dw);

			if ((*mip_test).dw != (*mip_dma).dw) {
				printf("\n");
				printf("test[%d][%d]: r=%x, g=%x, b=%x, a=%x, value=%x \n", x, y, mip_test->channel.r, mip_test->channel.g, mip_test->channel.b, mip_test->channel.a, mip_test->dw);
				printf("xdma[%d][%d]: r=%x, g=%x, b=%x, a=%x, value=%x \n", x, y, mip_dma->channel.r, mip_dma->channel.g, mip_dma->channel.b, mip_dma->channel.a, mip_dma->dw);
			}
		}
	}
}

static void gsgpu_command_submission_mipmap_generate_helper(unsigned ip_type, uint16_t width, uint16_t height)
{
        const int pm4_dw = 256;
        gsgpu_context_handle context_handle;
        gsgpu_bo_handle bo1, bo2;
        gsgpu_bo_handle *resources;
        uint32_t *pm4;
        struct gsgpu_cs_ib_info *ib_info;
        struct gsgpu_cs_request *ibs_request;
        uint64_t bo1_mc, bo2_mc;
        volatile union pixel_rgba8 *bo1_cpu, *bo2_cpu;
        int i, j, r, loop1, loop2, ring_id, align_dw;
        uint64_t gtt_flags[2] = {0};
        gsgpu_va_handle bo1_va_handle, bo2_va_handle;
        struct drm_gsgpu_info_hw_ip hw_ip_info;
	uint16_t dst_width = width;
	uint16_t dst_height = height;
	uint16_t src_width = dst_width * 2;
	uint16_t src_height = dst_height * 2;

	/*hardware will ride 4, software need round up*/
	if (0 != (dst_height % 4))
		dst_height = dst_height/4 + 1;
	else
		dst_height = dst_height/4;

	/* init object of command package */
	struct gsgpu_xdma_cmd_desc cmd_buffer[] = {
		{
			.header.sec = {
				GSGPU_CMD_XDMA_COPY,
				GSGPU_CMD_XDMA_FORMAT_RGBA8,
				GSGPU_CMD_XDMA_BODY_NR,
				GSGPU_CMD_XDMA_MODE_MIPMAP,
				GSGPU_CMD_XDMA_SUB_MODE_TILED_4X4,
			},
			.body = {
				.size.width = dst_width,
				.size.height = dst_height,
				.src_lo = 0,
				.src_hi = 0,
				.dst_lo = 0,
				.dst_hi = 0,
				.src_stride = src_width * 4 * 4,
				.dst_stride = src_width / 2 * 4 * 4,
				.sema.rd = 0,
				.sema.rd_en = 0,
				.sema.wr = 0,
				.sema.wr_en = 0,
			},
		},
	};

	pm4 = calloc(pm4_dw, sizeof(*pm4));
	CU_ASSERT_NOT_EQUAL(pm4, NULL);

	ib_info = calloc(1, sizeof(*ib_info));
	CU_ASSERT_NOT_EQUAL(ib_info, NULL);

	ibs_request = calloc(1, sizeof(*ibs_request));
	CU_ASSERT_NOT_EQUAL(ibs_request, NULL);

	r = gsgpu_query_hw_ip_info(device_handle, ip_type, 0, &hw_ip_info);
	CU_ASSERT_EQUAL(r, 0);

	r = gsgpu_cs_ctx_create(device_handle, &context_handle);
	CU_ASSERT_EQUAL(r, 0);

	/* prepare resource */
	resources = calloc(2, sizeof(gsgpu_bo_handle));
	CU_ASSERT_NOT_EQUAL(resources, NULL);

	for (int index = 0; index < TABLE_SIZE(cmd_buffer); index++) {
		int sdma_write_length = get_pixel_depth(cmd_buffer[index].header.sec.format) * src_width * src_height;
		/* allocate UC bo1for sDMA use */
		r = gsgpu_bo_alloc_and_map(device_handle,
					    sdma_write_length, 4096,
					    GSGPU_GEM_DOMAIN_GTT,
					    gtt_flags[loop1], &bo1,
					    (void**)&bo1_cpu, &bo1_mc,
					    &bo1_va_handle);
		CU_ASSERT_EQUAL(r, 0);


		memset((void*)bo1_cpu, 0, sdma_write_length);

		/* set bo1 */
		for (int i = 0; i < sdma_write_length/get_pixel_depth(cmd_buffer[index].header.sec.format); i++) {
			(*(bo1_cpu + i)).dw = rand() & 0xffffffff;
		}

		/* allocate UC bo2 for sDMA use */
		r = gsgpu_bo_alloc_and_map(device_handle,
					    sdma_write_length, 4096,
					    GSGPU_GEM_DOMAIN_GTT,
					    gtt_flags[loop2], &bo2,
					    (void**)&bo2_cpu, &bo2_mc,
					    &bo2_va_handle);
		CU_ASSERT_EQUAL(r, 0);

		/* clear bo2 */
		memset((void*)bo2_cpu, 0, sdma_write_length);

		resources[0] = bo1;
		resources[1] = bo2;

		/* fulfill PM4: test DMA mipmap reslove */
		cmd_buffer[index].body.src_lo = (uint32_t)(0xffffffff & bo1_mc);
		cmd_buffer[index].body.src_hi = (uint32_t)((0xffffffff00000000 & bo1_mc) >> 32);
		cmd_buffer[index].body.dst_lo = (uint32_t)(0xffffffff & bo2_mc);
		cmd_buffer[index].body.dst_hi = (uint32_t)((0xffffffff00000000 & bo2_mc) >> 32);

		i = sizeof(cmd_buffer[index]);
		memcpy(pm4, &cmd_buffer[index], i);

		i = (cmd_buffer[index].header.sec.length + 1);

		/* ib cmd packet align */
		align_dw = hw_ip_info.ib_size_alignment - (i & (hw_ip_info.ib_size_alignment - 1));
		for (j = 0; j < align_dw; j++) {
			pm4[i++] = GSGPU_CMD_NOP;
		}

		gsgpu_test_exec_cs_helper(context_handle,
					   ip_type, ring_id,
					   i, pm4,
					   2, resources,
					   ib_info, ibs_request);

		/* verify if SDMA test result meets with expected */
		verify_mipmaps((union pixel_rgba8 *)bo1_cpu, (union pixel_rgba8 *)bo2_cpu, dst_width, src_height/2);

		r = gsgpu_bo_unmap_and_free(bo1, bo1_va_handle, bo1_mc,
					     sdma_write_length);
		CU_ASSERT_EQUAL(r, 0);
		r = gsgpu_bo_unmap_and_free(bo2, bo2_va_handle, bo2_mc,
					     sdma_write_length);
		CU_ASSERT_EQUAL(r, 0);
	}

	/* clean resources */
	free(resources);
	free(ibs_request);
	free(ib_info);
	free(pm4);

	/* end of test */
	r = gsgpu_cs_ctx_free(context_handle);
	CU_ASSERT_EQUAL(r, 0);

}

static void gsgpu_command_submission_sdma_mipmap_generate(void)
{
	uint16_t width, height;

	for (int count = 0; count < 3; count++) {
		switch (count) {
		case MINIMUM:
			width = 1;
			height = 1;
			break;
		case EQUAL_LENGTH:
			width = 128;
			height = 128;
			break;
		case UNEQUAL_LENGTH:
			width = 512;
			height = 384;
			break;
		default:
			break;
		}

		gsgpu_command_submission_mipmap_generate_helper(GSGPU_HW_IP_DMA, width, height);
	}
}


#define PIXEL_FORMAT_RGBA8 4
static void generate_once_mipmap(union pixel_rgba8 * const src, union pixel_rgba8 * const dst, const uint16_t width, const uint16_t height)
{
	union pixel_rgba8 *test_src00, *test_src10, *test_src01, *test_src11;
	union pixel_rgba8 *test_dst;

	for (int y = 0; y < height; y++){
		for(int x = 0; x < width; x++){
			test_src00 = get_tile_pixel(src, 2*x+0, 2*y+0, width *2);
			test_src10 = get_tile_pixel(src, 2*x+1, 2*y+0, width *2);
			test_src01 = get_tile_pixel(src, 2*x+0, 2*y+1, width *2);
			test_src11 = get_tile_pixel(src, 2*x+1, 2*y+1, width* 2);
			test_dst = get_tile_pixel(dst, x, y , width);

			(*test_dst).channel.r = ((*test_src00).channel.r + (*test_src01).channel.r + (*test_src10).channel.r + (*test_src11).channel.r)/4;
			(*test_dst).channel.g = ((*test_src00).channel.g + (*test_src01).channel.g + (*test_src10).channel.g + (*test_src11).channel.g)/4;
			(*test_dst).channel.b = ((*test_src00).channel.b + (*test_src01).channel.b + (*test_src10).channel.b + (*test_src11).channel.b)/4;
			(*test_dst).channel.a = ((*test_src00).channel.a + (*test_src01).channel.a + (*test_src10).channel.a + (*test_src11).channel.a)/4;
		}
	}
}

static void gsgpu_command_submission_sdma_copy_tiled(void)
{
        gsgpu_command_submission_copy_tiled_helper(GSGPU_HW_IP_DMA);
}

static void gsgpu_command_submission_copy_tiled_helper(unsigned ip_type)
{
	const int pm4_dw = 256;
	gsgpu_context_handle context_handle = NULL;
	gsgpu_bo_handle bo1 = NULL, bo2 = NULL;
	gsgpu_bo_handle *resources;
	uint32_t *pm4;
	struct gsgpu_cs_ib_info *ib_info;
	struct gsgpu_cs_request *ibs_request;
	uint64_t bo1_mc, bo2_mc;
	volatile unsigned char *bo1_cpu, *bo2_cpu;
	int i, j, r, ring_id, align_dw;
	uint64_t gtt_flags[1] = {0};
	gsgpu_va_handle bo1_va_handle = NULL, bo2_va_handle = NULL;
	struct drm_gsgpu_info_hw_ip hw_ip_info;

	/* init object of command package */
	struct gsgpu_xdma_cmd_desc cmd_buffer[] = {
		{
			.header.sec = {
				GSGPU_CMD_XDMA_COPY,
				GSGPU_CMD_XDMA_FORMAT_RGBA8,
				GSGPU_CMD_XDMA_BODY_NR,
				GSGPU_CMD_XDMA_MODE_L2T,
				GSGPU_CMD_XDMA_SUB_MODE_TILED_4X4,
			},
			.body = {
				.size.width = 16,
				.size.height = 16,
				.src_lo = 0,
				.src_hi = 0,
				.dst_lo = 0,
				.dst_hi = 0,
				.src_stride = 16 * 4,
				.dst_stride = 16 * 4 * 4,
				.sema.rd = 0,
				.sema.rd_en = 0,
				.sema.wr = 0,
				.sema.wr_en = 0,
			},
		},
		{
			.header.sec = {
				GSGPU_CMD_XDMA_COPY,
				GSGPU_CMD_XDMA_FORMAT_RGBA8,
				GSGPU_CMD_XDMA_BODY_NR,
				GSGPU_CMD_XDMA_MDOE_T2L,
				GSGPU_CMD_XDMA_SUB_MODE_TILED_4X4,
			},
			.body = {
				.size.width = 16,
				.size.height = 16,
				.src_lo = 0,
				.src_hi = 0,
				.dst_lo = 0,
				.dst_hi = 0,
				.src_stride = 16 * 4 * 4,
				.dst_stride = 16 * 4,
				.sema.rd = 0,
				.sema.rd_en = 0,
				.sema.wr = 0,
				.sema.wr_en = 0,
			},
		},
	};

	pm4 = calloc(pm4_dw, sizeof(*pm4));
	CU_ASSERT_NOT_EQUAL(pm4, NULL);

	ib_info = calloc(1, sizeof(*ib_info));
	CU_ASSERT_NOT_EQUAL(ib_info, NULL);

	ibs_request = calloc(1, sizeof(*ibs_request));
	CU_ASSERT_NOT_EQUAL(ibs_request, NULL);

	r = gsgpu_query_hw_ip_info(device_handle, ip_type, 0, &hw_ip_info);
	CU_ASSERT_EQUAL(r, 0);

	r = gsgpu_cs_ctx_create(device_handle, &context_handle);
	CU_ASSERT_EQUAL(r, 0);

	/* prepare resource */
	resources = calloc(2, sizeof(gsgpu_bo_handle));
	CU_ASSERT_NOT_EQUAL(resources, NULL);

	for (int index = 0; index < TABLE_SIZE(cmd_buffer); index++) {
		int sdma_write_length = get_pixel_depth(cmd_buffer[index].header.sec.format) * cmd_buffer[index].body.size.width * cmd_buffer[index].body.size.height;
		/* allocate UC bo1for sDMA use */
		r = gsgpu_bo_alloc_and_map(device_handle,
					    sdma_write_length, 4096,
					    GSGPU_GEM_DOMAIN_GTT,
					    gtt_flags[0], &bo1,
					    (void**)&bo1_cpu, &bo1_mc,
					    &bo1_va_handle);
		CU_ASSERT_EQUAL(r, 0);

		/* set bo1 */
		memset((void*)bo1_cpu, 0xaa, sdma_write_length);

		/* allocate UC bo2 for sDMA use */
		r = gsgpu_bo_alloc_and_map(device_handle,
					    sdma_write_length, 4096,
					    GSGPU_GEM_DOMAIN_GTT,
					    gtt_flags[0], &bo2,
					    (void**)&bo2_cpu, &bo2_mc,
					    &bo2_va_handle);
		CU_ASSERT_EQUAL(r, 0);

		/* clear bo2 */
		memset((void*)bo2_cpu, 0, sdma_write_length);

		resources[0] = bo1;
		resources[1] = bo2;

		/* fulfill PM4: test DMA copy linear */
		cmd_buffer[index].body.src_lo = (uint32_t)(0xffffffff & bo1_mc);
		cmd_buffer[index].body.src_hi = (uint32_t)((0xffffffff00000000 & bo1_mc) >> 32);
		cmd_buffer[index].body.dst_lo = (uint32_t)(0xffffffff & bo2_mc);
		cmd_buffer[index].body.dst_hi = (uint32_t)((0xffffffff00000000 & bo2_mc) >> 32);

		i = sizeof(cmd_buffer[index]);
		memcpy(pm4, &cmd_buffer[index], i);

		i = (cmd_buffer[index].header.sec.length + 1);

		/* ib cmd packet align */
		align_dw = hw_ip_info.ib_size_alignment - (i & (hw_ip_info.ib_size_alignment - 1));
		for (j = 0; j < align_dw; j++) {
			pm4[i++] = GSGPU_CMD_NOP;
		}

		gsgpu_test_exec_cs_helper(context_handle,
					   ip_type, 0,
					   i, pm4,
					   2, resources,
					   ib_info, ibs_request);

		/* verify if SDMA test result meets with expected */
		i = 0;
		while(i < sdma_write_length) {
			CU_ASSERT_EQUAL(bo2_cpu[i++], 0xaa);
		}
		r = gsgpu_bo_unmap_and_free(bo1, bo1_va_handle, bo1_mc,
					     sdma_write_length);
		CU_ASSERT_EQUAL(r, 0);
		r = gsgpu_bo_unmap_and_free(bo2, bo2_va_handle, bo2_mc,
					     sdma_write_length);
		CU_ASSERT_EQUAL(r, 0);
	}
	/* clean resources */
	free(resources);
	free(ibs_request);
	free(ib_info);
	free(pm4);

	/* end of test */
	r = gsgpu_cs_ctx_free(context_handle);
	CU_ASSERT_EQUAL(r, 0);
}

static void gsgpu_command_submission_sdma_semaphore(void)
{
        gsgpu_command_submission_sdma_semaphore_helper(GSGPU_HW_IP_DMA);
}

static void gsgpu_command_submission_sdma_semaphore_helper(unsigned ip_type)
{
	const int pm4_dw = 256;
	gsgpu_context_handle context_handle;
	gsgpu_bo_handle bo;
	gsgpu_bo_handle *resources;
	uint32_t *pm4;
	struct gsgpu_cs_ib_info *ib_info;
	struct gsgpu_cs_request *ibs_request;
	uint64_t bo_mc;
	volatile uint32_t *bo_cpu;
	int i, j, r, align_dw;
	uint64_t gtt_flags[1] = {0};
	gsgpu_va_handle va_handle;
	struct drm_gsgpu_info_hw_ip hw_ip_info;

	struct gsgpu_xdma_cmd_desc cmd_buffer[] = {
		{
			.header.sec = {
				GSGPU_CMD_XDMA_COPY,
				GSGPU_CMD_XDMA_FORMAT_RGBA8,
				GSGPU_CMD_XDMA_BODY_NR,
				GSGPU_CMD_XDMA_MODE_MEMSET,
				GSGPU_CMD_XDMA_SUB_MODE_DEFAULT,
			},
			.body = {
				.size.width = 16 * 1024,
				.size.height = 1,
				.src_lo = 0xdeadbeaf,
				.src_hi = 0,
				.dst_lo = 0,
				.dst_hi = 0,
				.src_stride = 0,
				.dst_stride = 16 * 1024,
				.sema.val = 0,
			},
		},
	};

	pm4 = calloc(pm4_dw, sizeof(*pm4));
	CU_ASSERT_NOT_EQUAL(pm4, NULL);

	ib_info = calloc(1, sizeof(*ib_info));
	CU_ASSERT_NOT_EQUAL(ib_info, NULL);

	ibs_request = calloc(1, sizeof(*ibs_request));
	CU_ASSERT_NOT_EQUAL(ibs_request, NULL);

	r = gsgpu_query_hw_ip_info(device_handle, ip_type, 0, &hw_ip_info);
	CU_ASSERT_EQUAL(r, 0);

	r = gsgpu_cs_ctx_create(device_handle, &context_handle);
	CU_ASSERT_EQUAL(r, 0);

	/* prepare resource */
	resources = calloc(1, sizeof(gsgpu_bo_handle));
	CU_ASSERT_NOT_EQUAL(resources, NULL);

	int sdma_write_length = get_pixel_depth(cmd_buffer[0].header.sec.format) * cmd_buffer[0].body.size.width * cmd_buffer[0].body.size.height;

	/* allocate bo for DMA use */
	r = gsgpu_bo_alloc_and_map(device_handle,
				    sdma_write_length, BUFFER_ALIGN,
				    GSGPU_GEM_DOMAIN_GTT,
				    0, &bo,
				    (void**)&bo_cpu, &bo_mc,
				    &va_handle);
	CU_ASSERT_EQUAL(r, 0);

	/* clear bo */
	memset((void*)bo_cpu, 0, sdma_write_length);

	resources[0] = bo;

	uint64_t sema_rd = 0;
	gsgpu_hw_sema_get(device_handle, context_handle, &sema_rd);

	/* fulfill cmd packet: test SDMA const fill */
	i = 0;
	cmd_buffer[0].body.dst_lo = (uint32_t)(0xffffffff & bo_mc);
	cmd_buffer[0].body.dst_hi = (uint32_t)((0xffffffff00000000 & bo_mc) >> 32);
	cmd_buffer[0].body.sema.rd = sema_rd;
	cmd_buffer[0].body.sema.rd_en = 1;


        /* FILL get semaphore */
	pm4[i++] = cmd_buffer[0].header.val;
	pm4[i++] = cmd_buffer[0].body.data_size;
	pm4[i++] = cmd_buffer[0].body.src_lo;
	pm4[i++] = cmd_buffer[0].body.src_hi;
	pm4[i++] = cmd_buffer[0].body.dst_lo;
	pm4[i++] = cmd_buffer[0].body.dst_hi;
	pm4[i++] = cmd_buffer[0].body.src_stride;
	pm4[i++] = cmd_buffer[0].body.dst_stride;
	pm4[i++] = cmd_buffer[0].body.sema.val;

	/* POLL set semaphore */
        pm4[i++] = GSPKT(0x85, 6) | 0 << 8 /* ture */ | 1 << 12 /* reg/mem */;
        pm4[i++] = (bo_mc + 256*4) & 0xffffffff;
        pm4[i++] = ((bo_mc + 256*4) >> 32) & 0xffffffff;
        pm4[i++] = 0x00000000; /* reference value */
        pm4[i++] = 0xffffffff; /* and mask */
        pm4[i++] = (0xfff) << 16 | 2; /* retry count, poll interval */
        pm4[i++] = 0x10 | (sema_rd & 0x0f); /* set semaphore */

	/* ib cmd packet align */
	align_dw = hw_ip_info.ib_size_alignment - (i & (hw_ip_info.ib_size_alignment - 1));
	for (j = 0; j < align_dw; j++) {
		pm4[i++] = GSGPU_CMD_NOP;
	}

	gsgpu_test_exec_cs_helper(context_handle,
				   ip_type, 0,
				   i, pm4,
				   1, resources,
				   ib_info, ibs_request);

	/* verify if SDMA test result meets with expected */
	i = 0;
	while (i < sdma_write_length / get_pixel_depth(cmd_buffer[0].header.sec.format)) {
		CU_ASSERT_EQUAL(bo_cpu[i++], 0xdeadbeaf);
	}

	gsgpu_hw_sema_put(device_handle, context_handle, sema_rd);

	r = gsgpu_bo_unmap_and_free(bo, va_handle, bo_mc,
				     sdma_write_length);
	CU_ASSERT_EQUAL(r, 0);

	/* clean resources */
	free(resources);
	free(ibs_request);
	free(ib_info);
	free(pm4);

	/* end of test */
	r = gsgpu_cs_ctx_free(context_handle);
	CU_ASSERT_EQUAL(r, 0);
}

static uint32_t get_pixel_depth(uint32_t format)
{
	uint32_t cpp;

	switch (format) {
	case GSGPU_CMD_XDMA_FORMAT_R8:
	case GSGPU_CMD_XDMA_FORMAT_S8:
		cpp = 1;
		break;
	case GSGPU_CMD_XDMA_FORMAT_R16:
	case GSGPU_CMD_XDMA_FORMAT_RG8:
	case GSGPU_CMD_XDMA_FORMAT_RGB5A1:
	case GSGPU_CMD_XDMA_FORMAT_R5G6B5:
	case GSGPU_CMD_XDMA_FORMAT_D16:
		cpp = 2;
		break;
	case GSGPU_CMD_XDMA_FORMAT_D24:
		cpp = 3;
		break;
	case GSGPU_CMD_XDMA_FORMAT_RGBA8:
	case GSGPU_CMD_XDMA_FORMAT_RG16:
	case GSGPU_CMD_XDMA_FORMAT_RGB10A2:
	case GSGPU_CMD_XDMA_FORMAT_D24S8:
		cpp = 4;
		break;
	case GSGPU_CMD_XDMA_FORMAT_RGBA16:
		cpp = 8;
		break;
	default:
		cpp = 4;
		break;
	}

	return cpp;
}
