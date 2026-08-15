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

#include "CUnit/Basic.h"

#include "loonggpu_test.h"
#include "loonggpu_drm.h"
#include "loonggpu_internal.h"

#include <pthread.h>


/*
 * This defines the delay in MS after which memory location designated for
 * compression against reference value is written to, unblocking command
 * processor
 */
#define WRITE_MEM_ADDRESS_DELAY_MS 100

static  loonggpu_device_handle device_handle;
static  uint32_t  major_version;
static  uint32_t  minor_version;

static pthread_t stress_thread;
static uint32_t *ptr = NULL;

static void loonggpu_deadlock_helper(unsigned ip_type);
static void loonggpu_deadlock_gfx(void);
static void loonggpu_deadlock_dma(void);

CU_BOOL suite_deadlock_tests_enable(void)
{
	CU_BOOL enable = CU_TRUE;

	if (loonggpu_device_initialize(drm_loonggpu[0], &major_version,
					     &minor_version, &device_handle))
		return CU_FALSE;

	if (loonggpu_device_deinitialize(device_handle))
		return CU_FALSE;

	return enable;
}

int suite_deadlock_tests_init(void)
{
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

	return CUE_SUCCESS;
}

int suite_deadlock_tests_clean(void)
{
	int r = loonggpu_device_deinitialize(device_handle);

	if (r == 0)
		return CUE_SUCCESS;
	else
		return CUE_SCLEAN_FAILED;
}


CU_TestInfo deadlock_tests[] = {
	{ "gfx ring block test",  loonggpu_deadlock_gfx },
	{ "dma ring block test",  loonggpu_deadlock_dma },
	CU_TEST_INFO_NULL,
};

static void *write_mem_address(void *data)
{
	int i;

	/* useconds_t range is [0, 1,000,000] so use loop for waits > 1s */
	for (i = 0; i < WRITE_MEM_ADDRESS_DELAY_MS; i++)
		usleep(1000);

        while(NULL == ptr) {
                usleep(1000);
        }

	ptr[256] = 0x1;

	return 0;
}

static void loonggpu_deadlock_dma(void)
{
	loonggpu_deadlock_helper(LOONGGPU_HW_IP_DMA);
}
static void loonggpu_deadlock_gfx(void)
{
	loonggpu_deadlock_helper(LOONGGPU_HW_IP_GFX);
}

static void loonggpu_deadlock_helper(unsigned ip_type)
{
	loonggpu_context_handle context_handle;
	loonggpu_bo_handle ib_result_handle = NULL;
	void *ib_result_cpu;
	uint64_t ib_result_mc_address = 0;
	struct loonggpu_cs_request ibs_request;
	struct loonggpu_cs_ib_info ib_info;
	struct loonggpu_cs_fence fence_status;
	uint32_t expired;
	int i, r;
	loonggpu_bo_list_handle bo_list;
	loonggpu_va_handle va_handle = NULL;

	r = pthread_create(&stress_thread, NULL, write_mem_address, NULL);
	CU_ASSERT_EQUAL(r, 0);

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

	ptr = ib_result_cpu;

	ptr[0] = GSPKT(LOONGGPU_CMD_POLL, 6) | 4 << 8 /* not equal */ | 1 << 12 /* reg/mem */;
	ptr[1] = (ib_result_mc_address + 256*4) & 0xffffffff;
	ptr[2] = ((ib_result_mc_address + 256*4) >> 32) & 0xffffffff;
	ptr[3] = 0x00000000; /* reference value */
	ptr[4] = 0xffffffff; /* and mask */
	ptr[5] = (0xfff) << 16 | 2; /* retry count, poll interval */
	ptr[6] = 0x00;

	for (i = 7; i < 16; ++i)
		ptr[i] = 0x80; //NOP

	ptr[256] = 0x0; /* the memory we wait on to change */

	memset(&ib_info, 0, sizeof(struct loonggpu_cs_ib_info));
	ib_info.ib_mc_address = ib_result_mc_address;
	ib_info.size = 16;

	memset(&ibs_request, 0, sizeof(struct loonggpu_cs_request));
	ibs_request.ip_type = ip_type;
	ibs_request.ring = 0;
	ibs_request.number_of_ibs = 1;
	ibs_request.ibs = &ib_info;
	ibs_request.resources = bo_list;
	ibs_request.fence_info.handle = NULL;

	r = loonggpu_cs_submit(context_handle, 0,&ibs_request, 1);
	CU_ASSERT_EQUAL((r == 0 || r == -ECANCELED), 1);

	memset(&fence_status, 0, sizeof(struct loonggpu_cs_fence));
	fence_status.context = context_handle;
	fence_status.ip_type = ip_type;
	fence_status.ip_instance = 0;
	fence_status.ring = 0;
	fence_status.fence = ibs_request.seq_no;

	r = loonggpu_cs_query_fence_status(&fence_status,
			LOONGGPU_TIMEOUT_INFINITE,0, &expired);
	CU_ASSERT_EQUAL((r == 0 || r == -ECANCELED), 1);

	pthread_join(stress_thread, NULL);

	r = loonggpu_bo_list_destroy(bo_list);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_unmap_and_free(ib_result_handle, va_handle,
				     ib_result_mc_address, 4096);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_cs_ctx_free(context_handle);
	CU_ASSERT_EQUAL(r, 0);
}
