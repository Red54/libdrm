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

#include "CUnit/Basic.h"

#include "loonggpu_test.h"
#include "loonggpu_drm.h"

loonggpu_device_handle device_handle;
uint32_t  major_version;
uint32_t  minor_version;
uint32_t  family_id;

static void loonggpu_query_info_test(void);
static void loonggpu_command_submission_gfx(void);
static void loonggpu_command_submission_multi_fence(void);
static void loonggpu_userptr_test(void);
static void loonggpu_semaphore_test(void);
static void loonggpu_sync_dependency_test(void);
static void loonggpu_bo_eviction_test(void);

void loonggpu_command_submission_write_linear_helper(unsigned ip_type);
static void loonggpu_command_submission_const_fill_helper(unsigned ip_type);
static void loonggpu_command_submission_copy_linear_helper(unsigned ip_type);
static void loonggpu_command_submission_msaa_resolve_helper(unsigned ip_type);
void loonggpu_test_exec_cs_helper(loonggpu_context_handle context_handle,
				       unsigned ip_type,
				       int instance, int pm4_dw, uint32_t *pm4_src,
				       int res_cnt, loonggpu_bo_handle *resources,
				       struct loonggpu_cs_ib_info *ib_info,
				       struct loonggpu_cs_request *ibs_request);

CU_TestInfo basic_tests[] = {
	{ "Query Info Test",  loonggpu_query_info_test },
	{ "Userptr Test",  loonggpu_userptr_test },
	{ "bo eviction Test",  loonggpu_bo_eviction_test },
	{ "Command submission Test (GFX)",  loonggpu_command_submission_gfx },
	{ "Command submission Test (Multi-Fence)", loonggpu_command_submission_multi_fence },
	{ "SW semaphore Test",  loonggpu_semaphore_test },
	{ "Sync dependency Test",  loonggpu_sync_dependency_test },
	CU_TEST_INFO_NULL,
};

#define BUFFER_SIZE (16 * 1024)
#define BUFFER_ALIGN (16 * 1024)

#define SWAP_32(num) (((num & 0xff000000) >> 24) | \
		      ((num & 0x0000ff00) << 8) | \
		      ((num & 0x00ff0000) >> 8) | \
		      ((num & 0x000000ff) << 24))

#define CODE_OFFSET 512
#define DATA_OFFSET 1024

int suite_basic_tests_init(void)
{
	struct loonggpu_gpu_info gpu_info = {0};
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

	r = loonggpu_query_gpu_info(device_handle, &gpu_info);
	if (r)
		return CUE_SINIT_FAILED;

	family_id = gpu_info.family_id;

	return CUE_SUCCESS;
}

int suite_basic_tests_clean(void)
{
	int r = loonggpu_device_deinitialize(device_handle);

	if (r == 0)
		return CUE_SUCCESS;
	else
		return CUE_SCLEAN_FAILED;
}

static void loonggpu_query_info_test(void)
{
	struct loonggpu_gpu_info gpu_info = {0};
	uint32_t version, feature;
	int r;

	r = loonggpu_query_gpu_info(device_handle, &gpu_info);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_query_firmware_version(device_handle, LOONGGPU_INFO_FW_VCE, 0,
					  0, &version, &feature);
	CU_ASSERT_EQUAL(r, 0);
}

static void loonggpu_command_submission_gfx_separate_ibs(void)
{
	loonggpu_context_handle context_handle;
	loonggpu_bo_handle ib_result_handle, ib_result_ce_handle;
	void *ib_result_cpu, *ib_result_ce_cpu;
	uint64_t ib_result_mc_address, ib_result_ce_mc_address;
	struct loonggpu_cs_request ibs_request = {0};
	struct loonggpu_cs_ib_info ib_info[2];
	struct loonggpu_cs_fence fence_status = {0};
	uint32_t *ptr;
	uint32_t expired;
	loonggpu_bo_list_handle bo_list;
	loonggpu_va_handle va_handle, va_handle_ce;
	int r;

	r = loonggpu_cs_ctx_create(device_handle, &context_handle);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_alloc_and_map(device_handle, 4096, 4096,
				    LOONGGPU_GEM_DOMAIN_GTT, 0,
				    &ib_result_handle, &ib_result_cpu,
				    &ib_result_mc_address, &va_handle);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_alloc_and_map(device_handle, 4096, 4096,
				    LOONGGPU_GEM_DOMAIN_GTT, 0,
				    &ib_result_ce_handle, &ib_result_ce_cpu,
				    &ib_result_ce_mc_address, &va_handle_ce);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_get_bo_list(device_handle, ib_result_handle,
			       ib_result_ce_handle, &bo_list);
	CU_ASSERT_EQUAL(r, 0);

	memset(ib_info, 0, 2 * sizeof(struct loonggpu_cs_ib_info));

	/* IT_SET_CE_DE_COUNTERS */
	ptr = ib_result_ce_cpu;
	ptr[0] = LOONGGPU_CMD_NOP;
	ib_info[0].ib_mc_address = ib_result_ce_mc_address;
	ib_info[0].size = 1;
	ib_info[0].flags = LOONGGPU_IB_FLAG_CE;

	/* IT_WAIT_ON_CE_COUNTER */
	ptr = ib_result_cpu;
	ptr[0] = LOONGGPU_CMD_NOP;
	ib_info[1].ib_mc_address = ib_result_mc_address;
	ib_info[1].size = 1;

	ibs_request.ip_type = LOONGGPU_HW_IP_GFX;
	ibs_request.number_of_ibs = 2;
	ibs_request.ibs = ib_info;
	ibs_request.resources = bo_list;
	ibs_request.fence_info.handle = NULL;

	r = loonggpu_cs_submit(context_handle, 0,&ibs_request, 1);

	CU_ASSERT_EQUAL(r, 0);

	fence_status.context = context_handle;
	fence_status.ip_type = LOONGGPU_HW_IP_GFX;
	fence_status.ip_instance = 0;
	fence_status.fence = ibs_request.seq_no;

	r = loonggpu_cs_query_fence_status(&fence_status,
					 LOONGGPU_TIMEOUT_INFINITE,
					 0, &expired);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_unmap_and_free(ib_result_handle, va_handle,
				     ib_result_mc_address, 4096);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_unmap_and_free(ib_result_ce_handle, va_handle_ce,
				     ib_result_ce_mc_address, 4096);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_list_destroy(bo_list);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_cs_ctx_free(context_handle);
	CU_ASSERT_EQUAL(r, 0);

}

static void loonggpu_command_submission_gfx_shared_ib(void)
{
	loonggpu_context_handle context_handle;
	loonggpu_bo_handle ib_result_handle;
	void *ib_result_cpu;
	uint64_t ib_result_mc_address;
	struct loonggpu_cs_request ibs_request = {0};
	struct loonggpu_cs_ib_info ib_info[2];
	struct loonggpu_cs_fence fence_status = {0};
	uint32_t *ptr;
	uint32_t expired;
	loonggpu_bo_list_handle bo_list;
	loonggpu_va_handle va_handle;
	int r;

	r = loonggpu_cs_ctx_create(device_handle, &context_handle);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_alloc_and_map(device_handle, 4096, 4096,
				    LOONGGPU_GEM_DOMAIN_GTT, 0,
				    &ib_result_handle, &ib_result_cpu,
				    &ib_result_mc_address, &va_handle);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_get_bo_list(device_handle, ib_result_handle, NULL,
			       &bo_list);
	CU_ASSERT_EQUAL(r, 0);

	memset(ib_info, 0, 2 * sizeof(struct loonggpu_cs_ib_info));

	/* IT_SET_CE_DE_COUNTERS */
	ptr = ib_result_cpu;
	ptr[0] = LOONGGPU_CMD_NOP;

	ib_info[0].ib_mc_address = ib_result_mc_address;
	ib_info[0].size = 1;
	ib_info[0].flags = LOONGGPU_IB_FLAG_CE;

	ptr = (uint32_t *)ib_result_cpu + 4;
	ptr[0] = LOONGGPU_CMD_NOP;
	ib_info[1].ib_mc_address = ib_result_mc_address + 16;
	ib_info[1].size = 1;

	ibs_request.ip_type = LOONGGPU_HW_IP_GFX;
	ibs_request.number_of_ibs = 2;
	ibs_request.ibs = ib_info;
	ibs_request.resources = bo_list;
	ibs_request.fence_info.handle = NULL;

	r = loonggpu_cs_submit(context_handle, 0, &ibs_request, 1);

	CU_ASSERT_EQUAL(r, 0);

	fence_status.context = context_handle;
	fence_status.ip_type = LOONGGPU_HW_IP_GFX;
	fence_status.ip_instance = 0;
	fence_status.fence = ibs_request.seq_no;

	r = loonggpu_cs_query_fence_status(&fence_status,
					 LOONGGPU_TIMEOUT_INFINITE,
					 0, &expired);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_unmap_and_free(ib_result_handle, va_handle,
				     ib_result_mc_address, 4096);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_list_destroy(bo_list);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_cs_ctx_free(context_handle);
	CU_ASSERT_EQUAL(r, 0);
}

static void loonggpu_command_submission_gfx_cp_write_data(void)
{
	loonggpu_command_submission_write_linear_helper(LOONGGPU_HW_IP_GFX);
}

static void loonggpu_command_submission_gfx_cp_const_fill(void)
{
	loonggpu_command_submission_const_fill_helper(LOONGGPU_HW_IP_GFX);
}

static void loonggpu_command_submission_gfx_cp_copy_data(void)
{
	loonggpu_command_submission_copy_linear_helper(LOONGGPU_HW_IP_GFX);
}

static void loonggpu_bo_eviction_test(void)
{
	const int sdma_write_length = 1024;
	const int pm4_dw = 256;
	loonggpu_context_handle context_handle;
	loonggpu_bo_handle bo1, bo2, vram_max[2], gtt_max[2];
	loonggpu_bo_handle *resources;
	uint32_t *pm4;
	struct loonggpu_cs_ib_info *ib_info;
	struct loonggpu_cs_request *ibs_request;
	uint64_t bo1_mc, bo2_mc;
	volatile unsigned char *bo1_cpu, *bo2_cpu;
	int i, j, r, loop1, loop2, align_dw;
	uint64_t gtt_flags[2] = {0, 0};
	loonggpu_va_handle bo1_va_handle, bo2_va_handle;
	struct loonggpu_heap_info vram_info, gtt_info;
	struct drm_loonggpu_info_hw_ip hw_ip_info;

	r = loonggpu_query_hw_ip_info(device_handle, LOONGGPU_HW_IP_DMA, 0, &hw_ip_info);
	CU_ASSERT_EQUAL(r, 0);

	pm4 = calloc(pm4_dw, sizeof(*pm4));
	CU_ASSERT_NOT_EQUAL(pm4, NULL);

	ib_info = calloc(1, sizeof(*ib_info));
	CU_ASSERT_NOT_EQUAL(ib_info, NULL);

	ibs_request = calloc(1, sizeof(*ibs_request));
	CU_ASSERT_NOT_EQUAL(ibs_request, NULL);

	r = loonggpu_cs_ctx_create(device_handle, &context_handle);
	CU_ASSERT_EQUAL(r, 0);

	/* prepare resource */
	resources = calloc(4, sizeof(loonggpu_bo_handle));
	CU_ASSERT_NOT_EQUAL(resources, NULL);

	r = loonggpu_query_heap_info(device_handle, LOONGGPU_GEM_DOMAIN_VRAM,
				   0, &vram_info);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_alloc_wrap(device_handle, vram_info.max_allocation, 4096,
				 LOONGGPU_GEM_DOMAIN_VRAM, 0, &vram_max[0]);
	CU_ASSERT_EQUAL(r, 0);
	r = loonggpu_bo_alloc_wrap(device_handle, vram_info.max_allocation, 4096,
				 LOONGGPU_GEM_DOMAIN_VRAM, 0, &vram_max[1]);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_query_heap_info(device_handle, LOONGGPU_GEM_DOMAIN_GTT,
				   0, &gtt_info);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_alloc_wrap(device_handle, gtt_info.max_allocation, 4096,
				 LOONGGPU_GEM_DOMAIN_GTT, 0, &gtt_max[0]);
	CU_ASSERT_EQUAL(r, 0);
	r = loonggpu_bo_alloc_wrap(device_handle, gtt_info.max_allocation, 4096,
				 LOONGGPU_GEM_DOMAIN_GTT, 0, &gtt_max[1]);
	CU_ASSERT_EQUAL(r, 0);



	loop1 = loop2 = 0;
	/* run 9 circle to test all mapping combination */
	while(loop1 < 2) {
		while(loop2 < 2) {
			/* allocate UC bo1for sDMA use */
			r = loonggpu_bo_alloc_and_map(device_handle,
						    sdma_write_length, 4096,
						    LOONGGPU_GEM_DOMAIN_GTT,
						    gtt_flags[loop1], &bo1,
						    (void**)&bo1_cpu, &bo1_mc,
						    &bo1_va_handle);
			CU_ASSERT_EQUAL(r, 0);

			/* set bo1 */
			memset((void*)bo1_cpu, 0xaa, sdma_write_length);

			/* allocate UC bo2 for sDMA use */
			r = loonggpu_bo_alloc_and_map(device_handle,
						    sdma_write_length, 4096,
						    LOONGGPU_GEM_DOMAIN_GTT,
						    gtt_flags[loop2], &bo2,
						    (void**)&bo2_cpu, &bo2_mc,
						    &bo2_va_handle);
			CU_ASSERT_EQUAL(r, 0);

			/* clear bo2 */
			memset((void*)bo2_cpu, 0, sdma_write_length);

			resources[0] = bo1;
			resources[1] = bo2;
			resources[2] = vram_max[loop2];
			resources[3] = gtt_max[loop2];

                        /* fulfill PM4: test DMA copy linear */
                        i = j = 0;
                        uint32_t res_format = LOONGGPU_CMD_XDMA_FORMAT_RGBA16;
                        uint32_t op_mode = 1;
                        pm4[i++] = GSPKT(LOONGGPU_CMD_XDMA_COPY, 8) | (res_format << 8) | (op_mode << 24);
                        pm4[i++] = (1 << 16) | sdma_write_length/8;
                        pm4[i++] = 0xffffffff & bo1_mc;
                        pm4[i++] = (0xffffffff00000000 & bo1_mc) >> 32;
                        pm4[i++] = 0xffffffff & bo2_mc;
                        pm4[i++] = (0xffffffff00000000 & bo2_mc) >> 32;
                        pm4[i++] = sdma_write_length;
                        pm4[i++] = sdma_write_length;
                        pm4[i++] = 0;

			/* ib cmd packet align */
			align_dw = hw_ip_info.ib_size_alignment - (i & (hw_ip_info.ib_size_alignment - 1));
			for (j = 0; j < align_dw; j++) {
				pm4[i++] = LOONGGPU_CMD_NOP;
			}

			loonggpu_test_exec_cs_helper(context_handle,
						   LOONGGPU_HW_IP_DMA, 0,
						   i, pm4,
						   4, resources,
						   ib_info, ibs_request);

			/* verify if SDMA test result meets with expected */
			i = 0;
			while(i < sdma_write_length) {
				CU_ASSERT_EQUAL(bo2_cpu[i++], 0xaa);
			}
			r = loonggpu_bo_unmap_and_free(bo1, bo1_va_handle, bo1_mc,
						     sdma_write_length);
			CU_ASSERT_EQUAL(r, 0);
			r = loonggpu_bo_unmap_and_free(bo2, bo2_va_handle, bo2_mc,
						     sdma_write_length);
			CU_ASSERT_EQUAL(r, 0);
			loop2++;
		}
		loop2 = 0;
		loop1++;
	}
	loonggpu_bo_free(vram_max[0]);
	loonggpu_bo_free(vram_max[1]);
	loonggpu_bo_free(gtt_max[0]);
	loonggpu_bo_free(gtt_max[1]);
	/* clean resources */
	free(resources);
	free(ibs_request);
	free(ib_info);
	free(pm4);

	/* end of test */
	r = loonggpu_cs_ctx_free(context_handle);
	CU_ASSERT_EQUAL(r, 0);
}


static void loonggpu_command_submission_gfx(void)
{
	/* write data using the CP */
	loonggpu_command_submission_gfx_cp_write_data();
	/* separate IB buffers for multi-IB submission */
	loonggpu_command_submission_gfx_separate_ibs();
	/* shared IB buffer for multi-IB submission */
	loonggpu_command_submission_gfx_shared_ib();
}

static void loonggpu_semaphore_test(void)
{
	loonggpu_context_handle context_handle[2];
	loonggpu_semaphore_handle sem;
	loonggpu_bo_handle ib_result_handle[2];
	void *ib_result_cpu[2];
	uint64_t ib_result_mc_address[2];
	struct loonggpu_cs_request ibs_request[2] = {0};
	struct loonggpu_cs_ib_info ib_info[2] = {0};
	struct loonggpu_cs_fence fence_status = {0};
	uint32_t *ptr;
	uint32_t expired;
	uint32_t sdma_nop, gfx_nop;
	loonggpu_bo_list_handle bo_list[2];
	loonggpu_va_handle va_handle[2];
	int r, i;

	sdma_nop = gfx_nop = LOONGGPU_CMD_NOP;

	r = loonggpu_cs_create_semaphore(&sem);
	CU_ASSERT_EQUAL(r, 0);
	for (i = 0; i < 2; i++) {
		r = loonggpu_cs_ctx_create(device_handle, &context_handle[i]);
		CU_ASSERT_EQUAL(r, 0);

		r = loonggpu_bo_alloc_and_map(device_handle, 4096, 4096,
					    LOONGGPU_GEM_DOMAIN_GTT, 0,
					    &ib_result_handle[i], &ib_result_cpu[i],
					    &ib_result_mc_address[i], &va_handle[i]);
		CU_ASSERT_EQUAL(r, 0);

		r = loonggpu_get_bo_list(device_handle, ib_result_handle[i],
				       NULL, &bo_list[i]);
		CU_ASSERT_EQUAL(r, 0);
	}

	/* 1. same context different engine */
	ptr = ib_result_cpu[0];
	ptr[0] = sdma_nop;
	ib_info[0].ib_mc_address = ib_result_mc_address[0];
	ib_info[0].size = 1;

	ibs_request[0].ip_type = LOONGGPU_HW_IP_DMA;
	ibs_request[0].number_of_ibs = 1;
	ibs_request[0].ibs = &ib_info[0];
	ibs_request[0].resources = bo_list[0];
	ibs_request[0].fence_info.handle = NULL;
	r = loonggpu_cs_submit(context_handle[0], 0,&ibs_request[0], 1);
	CU_ASSERT_EQUAL(r, 0);
	r = loonggpu_cs_signal_semaphore(context_handle[0], LOONGGPU_HW_IP_DMA, 0, 0, sem);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_cs_wait_semaphore(context_handle[0], LOONGGPU_HW_IP_GFX, 0, 0, sem);
	CU_ASSERT_EQUAL(r, 0);
	ptr = ib_result_cpu[1];
	ptr[0] = gfx_nop;
	ib_info[1].ib_mc_address = ib_result_mc_address[1];
	ib_info[1].size = 1;

	ibs_request[1].ip_type = LOONGGPU_HW_IP_GFX;
	ibs_request[1].number_of_ibs = 1;
	ibs_request[1].ibs = &ib_info[1];
	ibs_request[1].resources = bo_list[1];
	ibs_request[1].fence_info.handle = NULL;

	r = loonggpu_cs_submit(context_handle[0], 0,&ibs_request[1], 1);
	CU_ASSERT_EQUAL(r, 0);

	fence_status.context = context_handle[0];
	fence_status.ip_type = LOONGGPU_HW_IP_GFX;
	fence_status.ip_instance = 0;
	fence_status.fence = ibs_request[1].seq_no;
	r = loonggpu_cs_query_fence_status(&fence_status,
					 5000000000000, 0, &expired);
	CU_ASSERT_EQUAL(r, 0);
	CU_ASSERT_EQUAL(expired, true);

	/* 2. same engine different context */
	ptr = ib_result_cpu[0];
	ptr[0] = gfx_nop;
	ib_info[0].ib_mc_address = ib_result_mc_address[0];
	ib_info[0].size = 1;

	ibs_request[0].ip_type = LOONGGPU_HW_IP_GFX;
	ibs_request[0].number_of_ibs = 1;
	ibs_request[0].ibs = &ib_info[0];
	ibs_request[0].resources = bo_list[0];
	ibs_request[0].fence_info.handle = NULL;
	r = loonggpu_cs_submit(context_handle[0], 0,&ibs_request[0], 1);
	CU_ASSERT_EQUAL(r, 0);
	r = loonggpu_cs_signal_semaphore(context_handle[0], LOONGGPU_HW_IP_GFX, 0, 0, sem);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_cs_wait_semaphore(context_handle[1], LOONGGPU_HW_IP_GFX, 0, 0, sem);
	CU_ASSERT_EQUAL(r, 0);
	ptr = ib_result_cpu[1];
	ptr[0] = gfx_nop;
	ib_info[1].ib_mc_address = ib_result_mc_address[1];
	ib_info[1].size = 1;

	ibs_request[1].ip_type = LOONGGPU_HW_IP_GFX;
	ibs_request[1].number_of_ibs = 1;
	ibs_request[1].ibs = &ib_info[1];
	ibs_request[1].resources = bo_list[1];
	ibs_request[1].fence_info.handle = NULL;
	r = loonggpu_cs_submit(context_handle[1], 0,&ibs_request[1], 1);

	CU_ASSERT_EQUAL(r, 0);

	fence_status.context = context_handle[1];
	fence_status.ip_type = LOONGGPU_HW_IP_GFX;
	fence_status.ip_instance = 0;
	fence_status.fence = ibs_request[1].seq_no;
	r = loonggpu_cs_query_fence_status(&fence_status,
					 50000000000000, 0, &expired);
	CU_ASSERT_EQUAL(r, 0);
	CU_ASSERT_EQUAL(expired, true);

	for (i = 0; i < 2; i++) {
		r = loonggpu_bo_unmap_and_free(ib_result_handle[i], va_handle[i],
					     ib_result_mc_address[i], 4096);
		CU_ASSERT_EQUAL(r, 0);

		r = loonggpu_bo_list_destroy(bo_list[i]);
		CU_ASSERT_EQUAL(r, 0);

		r = loonggpu_cs_ctx_free(context_handle[i]);
		CU_ASSERT_EQUAL(r, 0);
	}

	r = loonggpu_cs_destroy_semaphore(sem);
	CU_ASSERT_EQUAL(r, 0);
}

/*
 * caller need create/release:
 * pm4_src, resources, ib_info, and ibs_request
 * submit command stream described in ibs_request and wait for this IB accomplished
 */
void loonggpu_test_exec_cs_helper(loonggpu_context_handle context_handle,
				       unsigned ip_type,
				       int instance, int pm4_dw, uint32_t *pm4_src,
				       int res_cnt, loonggpu_bo_handle *resources,
				       struct loonggpu_cs_ib_info *ib_info,
				       struct loonggpu_cs_request *ibs_request)
{
	int r;
	uint32_t expired;
	uint32_t *ring_ptr;
	loonggpu_bo_handle ib_result_handle = NULL;
	void *ib_result_cpu;
	uint64_t ib_result_mc_address = 0;
	struct loonggpu_cs_fence fence_status = {0};
	loonggpu_bo_handle *all_res = alloca(sizeof(resources[0]) * (res_cnt + 1));
	loonggpu_va_handle va_handle = NULL;

	/* prepare CS */
	CU_ASSERT_NOT_EQUAL(pm4_src, NULL);
	CU_ASSERT_NOT_EQUAL(resources, NULL);
	CU_ASSERT_NOT_EQUAL(ib_info, NULL);
	CU_ASSERT_NOT_EQUAL(ibs_request, NULL);
	CU_ASSERT_TRUE(pm4_dw <= BUFFER_SIZE / 4);

	/* allocate IB */
	r = loonggpu_bo_alloc_and_map(device_handle, BUFFER_SIZE, BUFFER_ALIGN,
				    LOONGGPU_GEM_DOMAIN_GTT, 0,
				    &ib_result_handle, &ib_result_cpu,
				    &ib_result_mc_address, &va_handle);
	CU_ASSERT_EQUAL(r, 0);

	/* copy PM4 packet to ring from caller */
	ring_ptr = ib_result_cpu;
	memcpy(ring_ptr, pm4_src, pm4_dw * sizeof(*pm4_src));

	ib_info->ib_mc_address = ib_result_mc_address;
	ib_info->size = pm4_dw;

	ibs_request->ip_type = ip_type;
	ibs_request->ring = instance;
	ibs_request->number_of_ibs = 1;
	ibs_request->ibs = ib_info;
	ibs_request->fence_info.handle = NULL;

	memcpy(all_res, resources, sizeof(resources[0]) * res_cnt);
	all_res[res_cnt] = ib_result_handle;

	r = loonggpu_bo_list_create(device_handle, res_cnt+1, all_res,
				  NULL, &ibs_request->resources);
	CU_ASSERT_EQUAL(r, 0);

	CU_ASSERT_NOT_EQUAL(ibs_request, NULL);

	/* submit CS */
	r = loonggpu_cs_submit(context_handle, 0, ibs_request, 1);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_list_destroy(ibs_request->resources);
	CU_ASSERT_EQUAL(r, 0);

	fence_status.ip_type = ip_type;
	fence_status.ip_instance = 0;
	fence_status.ring = ibs_request->ring;
	fence_status.context = context_handle;
	fence_status.fence = ibs_request->seq_no;

	/* wait for IB accomplished */
	r = loonggpu_cs_query_fence_status(&fence_status,
					 LOONGGPU_TIMEOUT_INFINITE,
					 0, &expired);
	CU_ASSERT_EQUAL(r, 0);
	CU_ASSERT_EQUAL(expired, true);

	r = loonggpu_bo_unmap_and_free(ib_result_handle, va_handle,
				     ib_result_mc_address, BUFFER_SIZE);
	CU_ASSERT_EQUAL(r, 0);
}

void loonggpu_command_submission_write_linear_helper(unsigned ip_type)
{
	const int sdma_write_length = 1;
	const int pm4_dw = 256;
	loonggpu_context_handle context_handle;
	loonggpu_bo_handle bo;
	loonggpu_bo_handle *resources;
	uint32_t *pm4;
	struct loonggpu_cs_ib_info *ib_info;
	struct loonggpu_cs_request *ibs_request;
	uint64_t bo_mc = 0;
	volatile uint32_t *bo_cpu;
	int i, j, r, loop, ring_id;
	uint64_t gtt_flags[2] = {0};
	loonggpu_va_handle va_handle = NULL;
	struct drm_loonggpu_info_hw_ip hw_ip_info;

	pm4 = calloc(pm4_dw, sizeof(*pm4));
	CU_ASSERT_NOT_EQUAL(pm4, NULL);

	ib_info = calloc(1, sizeof(*ib_info));
	CU_ASSERT_NOT_EQUAL(ib_info, NULL);

	ibs_request = calloc(1, sizeof(*ibs_request));
	CU_ASSERT_NOT_EQUAL(ibs_request, NULL);

	r = loonggpu_query_hw_ip_info(device_handle, ip_type, 0, &hw_ip_info);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_cs_ctx_create(device_handle, &context_handle);
	CU_ASSERT_EQUAL(r, 0);

	/* prepare resource */
	resources = calloc(1, sizeof(loonggpu_bo_handle));
	CU_ASSERT_NOT_EQUAL(resources, NULL);

	for (ring_id = 0; (1 << ring_id) & hw_ip_info.available_rings; ring_id++) {
		loop = 0;
		while(loop < 1) {
			/* allocate UC bo for sDMA use */
			r = loonggpu_bo_alloc_and_map(device_handle,
						    sdma_write_length * sizeof(uint32_t),
						    4096, LOONGGPU_GEM_DOMAIN_GTT,
						    gtt_flags[loop], &bo, (void**)&bo_cpu,
						    &bo_mc, &va_handle);
			CU_ASSERT_EQUAL(r, 0);

			/* clear bo */
			memset((void*)bo_cpu, 0, sdma_write_length * sizeof(uint32_t));

			resources[0] = bo;

			/* fulfill PM4: test DMA write-linear */
			i = j = 0;

			pm4[i++] = GSPKT(LOONGGPU_CMD_WRITE, 3) | WRITE_DST_SEL(1);
			pm4[i++] = 0xffffffff & bo_mc;
			pm4[i++] = (0xffffffff00000000 & bo_mc) >> 32;
			pm4[i++] = 0xdeadbeaf;

			loonggpu_test_exec_cs_helper(context_handle,
						   ip_type, ring_id,
						   i, pm4,
						   1, resources,
						   ib_info, ibs_request);

			/* verify if SDMA test result meets with expected */
			i = 0;
			while(i < sdma_write_length) {
				CU_ASSERT_EQUAL(bo_cpu[i++], 0xdeadbeaf);
			}

			r = loonggpu_bo_unmap_and_free(bo, va_handle, bo_mc,
						     sdma_write_length * sizeof(uint32_t));
			CU_ASSERT_EQUAL(r, 0);
			loop++;
		}
	}
	/* clean resources */
	free(resources);
	free(ibs_request);
	free(ib_info);
	free(pm4);

	/* end of test */
	r = loonggpu_cs_ctx_free(context_handle);
	CU_ASSERT_EQUAL(r, 0);
}

static void loonggpu_command_submission_const_fill_helper(unsigned ip_type)
{
	const int sdma_write_length = 16 * 1024;
	const int pm4_dw = 256;
	loonggpu_context_handle context_handle;
	loonggpu_bo_handle bo;
	loonggpu_bo_handle *resources;
	uint32_t *pm4;
	struct loonggpu_cs_ib_info *ib_info;
	struct loonggpu_cs_request *ibs_request;
	uint64_t bo_mc;
	volatile uint32_t *bo_cpu;
	int i, j, r, loop, loop_cnt, ring_id, align_dw;
	uint64_t gtt_flags[1] = {0};
	loonggpu_va_handle va_handle;
	struct drm_loonggpu_info_hw_ip hw_ip_info;

	pm4 = calloc(pm4_dw, sizeof(*pm4));
	CU_ASSERT_NOT_EQUAL(pm4, NULL);

	ib_info = calloc(1, sizeof(*ib_info));
	CU_ASSERT_NOT_EQUAL(ib_info, NULL);

	ibs_request = calloc(1, sizeof(*ibs_request));
	CU_ASSERT_NOT_EQUAL(ibs_request, NULL);

	r = loonggpu_query_hw_ip_info(device_handle, ip_type, 0, &hw_ip_info);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_cs_ctx_create(device_handle, &context_handle);
	CU_ASSERT_EQUAL(r, 0);

	/* prepare resource */
	resources = calloc(1, sizeof(loonggpu_bo_handle));
	CU_ASSERT_NOT_EQUAL(resources, NULL);

	for (ring_id = 0; (1 << ring_id) & hw_ip_info.available_rings; ring_id++) {
		loop = 0;
		loop_cnt = sizeof(gtt_flags) / sizeof(gtt_flags[0]);

		while(loop < loop_cnt) {
			/* allocate bo for DMA use */
			r = loonggpu_bo_alloc_and_map(device_handle,
						    sdma_write_length, BUFFER_ALIGN,
						    LOONGGPU_GEM_DOMAIN_GTT,
						    gtt_flags[loop], &bo, (void**)&bo_cpu,
						    &bo_mc, &va_handle);
			CU_ASSERT_EQUAL(r, 0);

			/* clear bo */
			memset((void*)bo_cpu, 0, sdma_write_length);

			resources[0] = bo;

			/* fulfill cmd packet: test SDMA const fill */
			i = 0;
			pm4[i++] = GSPKT(LOONGGPU_CMD_XDMA_COPY, 4);
			pm4[i++] = (uint32_t)(0xffffffff & bo_mc);
			pm4[i++] = (uint32_t)((0xffffffff00000000 & bo_mc) >> 32);
			pm4[i++] = sdma_write_length;
			pm4[i++] = 0xdeadbeaf;

			/* ib cmd packet align */
			align_dw = hw_ip_info.ib_size_alignment - (i & (hw_ip_info.ib_size_alignment - 1));
			for (j = 0; j < align_dw; j++) {
				pm4[i++] = LOONGGPU_CMD_NOP;
			}

			loonggpu_test_exec_cs_helper(context_handle,
						   ip_type, ring_id,
						   i, pm4,
						   1, resources,
						   ib_info, ibs_request);

			/* verify if SDMA test result meets with expected */
			i = 0;
			while (i < (sdma_write_length / 4)) {
				CU_ASSERT_EQUAL(bo_cpu[i++], 0xdeadbeaf);
			}

			r = loonggpu_bo_unmap_and_free(bo, va_handle, bo_mc,
						     sdma_write_length);
			CU_ASSERT_EQUAL(r, 0);
			loop++;
		}
	}
	/* clean resources */
	free(resources);
	free(ibs_request);
	free(ib_info);
	free(pm4);

	/* end of test */
	r = loonggpu_cs_ctx_free(context_handle);
	CU_ASSERT_EQUAL(r, 0);
}

static void loonggpu_command_submission_copy_linear_helper(unsigned ip_type)
{
	const int sdma_write_length = 1024;
	const int pm4_dw = 256;
	loonggpu_context_handle context_handle = NULL;
	loonggpu_bo_handle bo1 = NULL, bo2 = NULL;
	loonggpu_bo_handle *resources;
	uint32_t *pm4;
	struct loonggpu_cs_ib_info *ib_info;
	struct loonggpu_cs_request *ibs_request;
	uint64_t bo1_mc, bo2_mc;
	volatile unsigned char *bo1_cpu, *bo2_cpu;
	int i, j, r, loop1, loop2, ring_id, align_dw;
	uint64_t gtt_flags[2] = {0};
	loonggpu_va_handle bo1_va_handle = NULL, bo2_va_handle = NULL;
	struct drm_loonggpu_info_hw_ip hw_ip_info;

	pm4 = calloc(pm4_dw, sizeof(*pm4));
	CU_ASSERT_NOT_EQUAL(pm4, NULL);

	ib_info = calloc(1, sizeof(*ib_info));
	CU_ASSERT_NOT_EQUAL(ib_info, NULL);

	ibs_request = calloc(1, sizeof(*ibs_request));
	CU_ASSERT_NOT_EQUAL(ibs_request, NULL);

	r = loonggpu_query_hw_ip_info(device_handle, ip_type, 0, &hw_ip_info);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_cs_ctx_create(device_handle, &context_handle);
	CU_ASSERT_EQUAL(r, 0);

	/* prepare resource */
	resources = calloc(2, sizeof(loonggpu_bo_handle));
	CU_ASSERT_NOT_EQUAL(resources, NULL);

	for (ring_id = 0; (1 << ring_id) & hw_ip_info.available_rings; ring_id++) {
		loop1 = loop2 = 0;
		/* run 9 circle to test all mapping combination */
		while(loop1 < 2) {
			while(loop2 < 2) {
				/* allocate UC bo1for sDMA use */
				r = loonggpu_bo_alloc_and_map(device_handle,
							    sdma_write_length, 4096,
							    LOONGGPU_GEM_DOMAIN_GTT,
							    gtt_flags[loop1], &bo1,
							    (void**)&bo1_cpu, &bo1_mc,
							    &bo1_va_handle);
				CU_ASSERT_EQUAL(r, 0);

				/* set bo1 */
				memset((void*)bo1_cpu, 0xaa, sdma_write_length);

				/* allocate UC bo2 for sDMA use */
				r = loonggpu_bo_alloc_and_map(device_handle,
							    sdma_write_length, 4096,
							    LOONGGPU_GEM_DOMAIN_GTT,
							    gtt_flags[loop2], &bo2,
							    (void**)&bo2_cpu, &bo2_mc,
							    &bo2_va_handle);
				CU_ASSERT_EQUAL(r, 0);

				/* clear bo2 */
				memset((void*)bo2_cpu, 0, sdma_write_length);

				resources[0] = bo1;
				resources[1] = bo2;

                                /* fulfill PM4: test DMA copy linear */
                                i = j = 0;
                                uint32_t res_format = LOONGGPU_CMD_XDMA_FORMAT_RGBA16;
                                uint32_t op_mode = 1;
                                pm4[i++] = GSPKT(LOONGGPU_CMD_XDMA_COPY, 8) | (res_format << 8) | (op_mode << 24);
                                pm4[i++] = (1 << 16) | sdma_write_length/8;
                                pm4[i++] = 0xffffffff & bo1_mc;
                                pm4[i++] = (0xffffffff00000000 & bo1_mc) >> 32;
                                pm4[i++] = 0xffffffff & bo2_mc;
                                pm4[i++] = (0xffffffff00000000 & bo2_mc) >> 32;
                                pm4[i++] = sdma_write_length;
                                pm4[i++] = sdma_write_length;
                                pm4[i++] = 0;
				/* ib cmd packet align */
				align_dw = hw_ip_info.ib_size_alignment - (i & (hw_ip_info.ib_size_alignment - 1));
				for (j = 0; j < align_dw; j++) {
					pm4[i++] = LOONGGPU_CMD_NOP;
				}

				loonggpu_test_exec_cs_helper(context_handle,
							   ip_type, ring_id,
							   i, pm4,
							   2, resources,
							   ib_info, ibs_request);

				/* verify if SDMA test result meets with expected */
				i = 0;
				while(i < sdma_write_length) {
					CU_ASSERT_EQUAL(bo2_cpu[i++], 0xaa);
				}
				r = loonggpu_bo_unmap_and_free(bo1, bo1_va_handle, bo1_mc,
							     sdma_write_length);
				CU_ASSERT_EQUAL(r, 0);
				r = loonggpu_bo_unmap_and_free(bo2, bo2_va_handle, bo2_mc,
							     sdma_write_length);
				CU_ASSERT_EQUAL(r, 0);
				loop2++;
			}
			loop1++;
		}
	}
	/* clean resources */
	free(resources);
	free(ibs_request);
	free(ib_info);
	free(pm4);

	/* end of test */
	r = loonggpu_cs_ctx_free(context_handle);
	CU_ASSERT_EQUAL(r, 0);
}

static void loonggpu_command_submission_multi_fence_wait_all(bool wait_all)
{
	loonggpu_context_handle context_handle;
	loonggpu_bo_handle ib_result_handle, ib_result_ce_handle;
	void *ib_result_cpu, *ib_result_ce_cpu;
	uint64_t ib_result_mc_address, ib_result_ce_mc_address;
	struct loonggpu_cs_request ibs_request[2] = {0};
	struct loonggpu_cs_ib_info ib_info[2];
	struct loonggpu_cs_fence fence_status[2] = {0};
	uint32_t *ptr;
	uint32_t expired;
	loonggpu_bo_list_handle bo_list;
	loonggpu_va_handle va_handle, va_handle_ce;
	int r;
	int i = 0, ib_cs_num = 2;

	r = loonggpu_cs_ctx_create(device_handle, &context_handle);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_alloc_and_map(device_handle, 4096, 4096,
				    LOONGGPU_GEM_DOMAIN_GTT, 0,
				    &ib_result_handle, &ib_result_cpu,
				    &ib_result_mc_address, &va_handle);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_alloc_and_map(device_handle, 4096, 4096,
				    LOONGGPU_GEM_DOMAIN_GTT, 0,
				    &ib_result_ce_handle, &ib_result_ce_cpu,
				    &ib_result_ce_mc_address, &va_handle_ce);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_get_bo_list(device_handle, ib_result_handle,
			       ib_result_ce_handle, &bo_list);
	CU_ASSERT_EQUAL(r, 0);

	memset(ib_info, 0, 2 * sizeof(struct loonggpu_cs_ib_info));

	/* IT_SET_CE_DE_COUNTERS */
	ptr = ib_result_ce_cpu;
	ptr[0] = LOONGGPU_CMD_NOP;
	ib_info[0].ib_mc_address = ib_result_ce_mc_address;
	ib_info[0].size = 1;
	ib_info[0].flags = LOONGGPU_IB_FLAG_CE;

	/* IT_WAIT_ON_CE_COUNTER */
	ptr = ib_result_cpu;
	ptr[0] = LOONGGPU_CMD_NOP;
	ib_info[1].ib_mc_address = ib_result_mc_address;
	ib_info[1].size = 1;

	for (i = 0; i < ib_cs_num; i++) {
		ibs_request[i].ip_type = LOONGGPU_HW_IP_GFX;
		ibs_request[i].number_of_ibs = 2;
		ibs_request[i].ibs = ib_info;
		ibs_request[i].resources = bo_list;
		ibs_request[i].fence_info.handle = NULL;
	}

	r = loonggpu_cs_submit(context_handle, 0,ibs_request, ib_cs_num);

	CU_ASSERT_EQUAL(r, 0);

	for (i = 0; i < ib_cs_num; i++) {
		fence_status[i].context = context_handle;
		fence_status[i].ip_type = LOONGGPU_HW_IP_GFX;
		fence_status[i].fence = ibs_request[i].seq_no;
	}

	r = loonggpu_cs_wait_fences(fence_status, ib_cs_num, wait_all,
				LOONGGPU_TIMEOUT_INFINITE,
				&expired, NULL);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_unmap_and_free(ib_result_handle, va_handle,
				     ib_result_mc_address, 4096);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_unmap_and_free(ib_result_ce_handle, va_handle_ce,
				     ib_result_ce_mc_address, 4096);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_list_destroy(bo_list);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_cs_ctx_free(context_handle);
	CU_ASSERT_EQUAL(r, 0);
}

static void loonggpu_command_submission_multi_fence(void)
{
	loonggpu_command_submission_multi_fence_wait_all(true);
	loonggpu_command_submission_multi_fence_wait_all(false);
}

static void loonggpu_userptr_test(void)
{
	int i, r;
	uint32_t *pm4 = NULL;
	uint64_t bo_mc;
	void *ptr = NULL;
	int pm4_dw = 256;
	loonggpu_bo_handle handle;
	loonggpu_context_handle context_handle;
	struct loonggpu_cs_ib_info *ib_info;
	struct loonggpu_cs_request *ibs_request;
	loonggpu_bo_handle buf_handle;
	loonggpu_va_handle va_handle;

	pm4 = calloc(pm4_dw, sizeof(*pm4));
	CU_ASSERT_NOT_EQUAL(pm4, NULL);

	ib_info = calloc(1, sizeof(*ib_info));
	CU_ASSERT_NOT_EQUAL(ib_info, NULL);

	ibs_request = calloc(1, sizeof(*ibs_request));
	CU_ASSERT_NOT_EQUAL(ibs_request, NULL);

	r = loonggpu_cs_ctx_create(device_handle, &context_handle);
	CU_ASSERT_EQUAL(r, 0);

	r = posix_memalign(&ptr, BUFFER_ALIGN, BUFFER_SIZE);
	if (r) {
		fprintf(stderr, "loonggpu_userptr_test : posix_memalign alloc failed\r\n");
		return;
	}

	CU_ASSERT_NOT_EQUAL(ptr, NULL);
	memset(ptr, 0, BUFFER_SIZE);

	r = loonggpu_create_bo_from_user_mem(device_handle,
								ptr, BUFFER_SIZE, &buf_handle);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_va_range_alloc(device_handle,
						loonggpu_gpu_va_range_general,
						BUFFER_SIZE, 1, 0, &bo_mc,
						&va_handle, 0);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_va_op(buf_handle, 0, BUFFER_SIZE, bo_mc, 0, LOONGGPU_VA_OP_MAP);
	CU_ASSERT_EQUAL(r, 0);

	handle = buf_handle;

	i = 0;

	pm4[i++] = GSPKT(LOONGGPU_CMD_WRITE, 3) | WRITE_DST_SEL(1) | WRITE_WAIT;
	pm4[i++] = (uint32_t)(0xffffffff & bo_mc);
	pm4[i++] = (uint32_t)((0xffffffff00000000 & bo_mc) >> 32);
	pm4[i++] = 0xdeadbeaf;

	if (!fork()) {
		pm4[0] = 0x0;
		exit(0);
	}

	loonggpu_test_exec_cs_helper(context_handle,
						LOONGGPU_HW_IP_GFX, 0,
						i, pm4,
						1, &handle,
						ib_info, ibs_request);

	CU_ASSERT_EQUAL(((int*)ptr)[0], 0xdeadbeaf);

	free(ibs_request);
	free(ib_info);
	free(pm4);

	r = loonggpu_bo_va_op(buf_handle, 0, BUFFER_SIZE, bo_mc, 0, LOONGGPU_VA_OP_UNMAP);
	CU_ASSERT_EQUAL(r, 0);
	r = loonggpu_va_range_free(va_handle);
	CU_ASSERT_EQUAL(r, 0);
	r = loonggpu_bo_free(buf_handle);
	CU_ASSERT_EQUAL(r, 0);
	free(ptr);

	r = loonggpu_cs_ctx_free(context_handle);
	CU_ASSERT_EQUAL(r, 0);

	wait(NULL);
}

static void loonggpu_sync_dependency_test(void)
{
}
