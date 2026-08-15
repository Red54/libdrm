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

#ifndef _LOONGGPU_TEST_H_
#define _LOONGGPU_TEST_H_

#include "loonggpu.h"
#include "loonggpu_drm.h"

/**
 * Define max. number of card in system which we are able to handle
 */
#define MAX_CARDS_SUPPORTED     8

/* Forward reference for array to keep "drm" handles */
extern int drm_loonggpu[MAX_CARDS_SUPPORTED];

/* Global variables */
extern int open_render_node;

/*************************  Basic test suite ********************************/

/*
 * Define basic test suite to serve as the starting point for future testing
*/

/**
 * Initialize basic test suite
 */
int suite_basic_tests_init();

/**
 * Deinitialize basic test suite
 */
int suite_basic_tests_clean();

/**
 * Tests in basic test suite
 */
extern CU_TestInfo basic_tests[];

/**
 * Initialize bo test suite
 */
int suite_bo_tests_init();

/**
 * Deinitialize bo test suite
 */
int suite_bo_tests_clean();

/**
 * Tests in bo test suite
 */
extern CU_TestInfo bo_tests[];

/**
 *  * Initialize dma test suite
 *   */
int suite_dma_tests_init();

/**
 *  * Deinitialize dma test suite
 *   */
int suite_dma_tests_clean();

/**
 *  * Tests in dma test suite
 *   */
extern CU_TestInfo dma_tests[];

/**
 * Initialize deadlock test suite
 */
int suite_deadlock_tests_init();

/**
 * Deinitialize deadlock test suite
 */
int suite_deadlock_tests_clean();

/**
 * Decide if the suite is enabled by default or not.
 */
CU_BOOL suite_deadlock_tests_enable(void);

/**
 * Tests in deadlock test suite
 */
extern CU_TestInfo deadlock_tests[];

/**
 * Initialize vm test suite
 */
int suite_vm_tests_init();

/**
 * Deinitialize deadlock test suite
 */
int suite_vm_tests_clean();

/**
 * Decide if the suite is enabled by default or not.
 */
CU_BOOL suite_vm_tests_enable(void);

/**
 * Tests in vm test suite
 */
extern CU_TestInfo vm_tests[];

/**
 * Helper functions
 */
static inline loonggpu_bo_handle gpu_mem_alloc(
					loonggpu_device_handle device_handle,
					uint64_t size,
					uint64_t alignment,
					uint32_t type,
					uint64_t flags,
					uint64_t *vmc_addr,
					loonggpu_va_handle *va_handle)
{
	struct loonggpu_bo_alloc_request req = {0};
	loonggpu_bo_handle buf_handle;
	int r;

	CU_ASSERT_NOT_EQUAL(vmc_addr, NULL);

	req.alloc_size = size;
	req.phys_alignment = alignment;
	req.preferred_heap = type;
	req.flags = flags;

	r = loonggpu_bo_alloc(device_handle, &req, &buf_handle);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_va_range_alloc(device_handle,
				  loonggpu_gpu_va_range_general,
				  size, alignment, 0, vmc_addr,
				  va_handle, 0);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_va_op(buf_handle, 0, size, *vmc_addr, 0, LOONGGPU_VA_OP_MAP);
	CU_ASSERT_EQUAL(r, 0);

	return buf_handle;
}

static inline int gpu_mem_free(loonggpu_bo_handle bo,
			       loonggpu_va_handle va_handle,
			       uint64_t vmc_addr,
			       uint64_t size)
{
	int r;

	r = loonggpu_bo_va_op(bo, 0, size, vmc_addr, 0, LOONGGPU_VA_OP_UNMAP);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_va_range_free(va_handle);
	CU_ASSERT_EQUAL(r, 0);

	r = loonggpu_bo_free(bo);
	CU_ASSERT_EQUAL(r, 0);

	return 0;
}

static inline int
loonggpu_bo_alloc_wrap(loonggpu_device_handle dev, unsigned size,
		     unsigned alignment, unsigned heap, uint64_t flags,
		     loonggpu_bo_handle *bo)
{
	struct loonggpu_bo_alloc_request request = {};
	loonggpu_bo_handle buf_handle;
	int r;

	request.alloc_size = size;
	request.phys_alignment = alignment;
	request.preferred_heap = heap;
	request.flags = flags;

	r = loonggpu_bo_alloc(dev, &request, &buf_handle);
	if (r)
		return r;

	*bo = buf_handle;

	return 0;
}

static inline int
loonggpu_bo_alloc_and_map(loonggpu_device_handle dev, unsigned size,
			unsigned alignment, unsigned heap, uint64_t flags,
			loonggpu_bo_handle *bo, void **cpu, uint64_t *mc_address,
			loonggpu_va_handle *va_handle)
{
	struct loonggpu_bo_alloc_request request = {};
	loonggpu_bo_handle buf_handle;
	loonggpu_va_handle handle;
	uint64_t vmc_addr;
	int r;

	request.alloc_size = size;
	request.phys_alignment = alignment;
	request.preferred_heap = heap;
	request.flags = flags;

	r = loonggpu_bo_alloc(dev, &request, &buf_handle);
	if (r)
		return r;

	r = loonggpu_va_range_alloc(dev,
				  loonggpu_gpu_va_range_general,
				  size, alignment, 0, &vmc_addr,
				  &handle, 0);
	if (r)
		goto error_va_alloc;

	r = loonggpu_bo_va_op(buf_handle, 0, size, vmc_addr, 0, LOONGGPU_VA_OP_MAP);
	if (r)
		goto error_va_map;

	r = loonggpu_bo_cpu_map(buf_handle, cpu);
	if (r)
		goto error_cpu_map;

	*bo = buf_handle;
	*mc_address = vmc_addr;
	*va_handle = handle;

	return 0;

error_cpu_map:
	loonggpu_bo_cpu_unmap(buf_handle);

error_va_map:
	loonggpu_bo_va_op(buf_handle, 0, size, vmc_addr, 0, LOONGGPU_VA_OP_UNMAP);

error_va_alloc:
	loonggpu_bo_free(buf_handle);
	return r;
}

static inline int
loonggpu_bo_unmap_and_free(loonggpu_bo_handle bo, loonggpu_va_handle va_handle,
			 uint64_t mc_addr, uint64_t size)
{
	loonggpu_bo_cpu_unmap(bo);
	loonggpu_bo_va_op(bo, 0, size, mc_addr, 0, LOONGGPU_VA_OP_UNMAP);
	loonggpu_va_range_free(va_handle);
	loonggpu_bo_free(bo);

	return 0;

}

static inline int
loonggpu_get_bo_list(loonggpu_device_handle dev, loonggpu_bo_handle bo1,
		   loonggpu_bo_handle bo2, loonggpu_bo_list_handle *list)
{
	loonggpu_bo_handle resources[] = {bo1, bo2};

	return loonggpu_bo_list_create(dev, bo2 ? 2 : 1, resources, NULL, list);
}


static inline CU_ErrorCode loonggpu_set_suite_active(const char *suite_name,
							  CU_BOOL active)
{
	CU_ErrorCode r = CU_set_suite_active(CU_get_suite(suite_name), active);

	if (r != CUE_SUCCESS)
		fprintf(stderr, "Failed to obtain suite %s\n", suite_name);

	return r;
}

static inline CU_ErrorCode loonggpu_set_test_active(const char *suite_name,
				  const char *test_name, CU_BOOL active)
{
	CU_ErrorCode r;
	CU_pSuite pSuite = CU_get_suite(suite_name);

	if (!pSuite) {
		fprintf(stderr, "Failed to obtain suite %s\n",
				suite_name);
		return CUE_NOSUITE;
	}

	r = CU_set_test_active(CU_get_test(pSuite, test_name), active);
	if (r != CUE_SUCCESS)
		fprintf(stderr, "Failed to obtain test %s\n", test_name);

	return r;
}

#define GSPKT(op, n)    (((op) & 0xFF) | ((n) & 0xFFFF) << 16)

#define LOONGGPU_CMD_NOP                   0x80
#define LOONGGPU_CMD_WRITE                 0x81
#define LOONGGPU_CMD_INDIRECT              0x82
#define LOONGGPU_CMD_FENCE                 0x83
#define LOONGGPU_CMD_TRAP                  0x84
#define LOONGGPU_CMD_POLL                  0x85

#define LOONGGPU_CMD_XDMA_COPY         0xc0

#define LOONGGPU_CMD_XDMA_FORMAT_RGBA8        0
#define LOONGGPU_CMD_XDMA_FORMAT_RGBA16       1
#define LOONGGPU_CMD_XDMA_FORMAT_RG8          10
#define LOONGGPU_CMD_XDMA_FORMAT_RG16         11
#define LOONGGPU_CMD_XDMA_FORMAT_R8           20
#define LOONGGPU_CMD_XDMA_FORMAT_R16          21
#define LOONGGPU_CMD_XDMA_FORMAT_RGB10A2      30
#define LOONGGPU_CMD_XDMA_FORMAT_RGB5A1       33
#define LOONGGPU_CMD_XDMA_FORMAT_R5G6B5       34
#define LOONGGPU_CMD_XDMA_FORMAT_D16          36
#define LOONGGPU_CMD_XDMA_FORMAT_D24          37
#define LOONGGPU_CMD_XDMA_FORMAT_D24S8        39
#define LOONGGPU_CMD_XDMA_FORMAT_S8           41

#define LOONGGPU_CMD_XDMA_MODE_L2L             1
#define LOONGGPU_CMD_XDMA_MODE_L2T             2
#define LOONGGPU_CMD_XDMA_MDOE_T2L             3
#define LOONGGPU_CMD_XDMA_MODE_MSAA            4
#define LOONGGPU_CMD_XDMA_MODE_MIPMAP          5
#define LOONGGPU_CMD_XDMA_MODE_MEMSET          7

#define LOONGGPU_CMD_XDMA_SUB_MODE_DEFAULT		0
#define LOONGGPU_CMD_XDMA_SUB_MODE_TILED_4X4   0
#define LOONGGPU_CMD_XDMA_SUB_MODE_TILED_8X8   1

#define LOONGGPU_CMD_XDMA_SUB_MODE_PAGE_GEN_PTEPDE		0x1
#define LOONGGPU_CMD_XDMA_SUB_MODE_PAGE_SIZE_4K		(0x0 << 1)
#define LOONGGPU_CMD_XDMA_SUB_MODE_PAGE_SIZE_16K		(0x1 << 1)
#define LOONGGPU_CMD_XDMA_SUB_MODE_PAGE_SIZE_2M   		(0x2 << 1)
#define LOONGGPU_CMD_XDMA_SUB_MODE_PAGE_SIZE_32M		(0x3 << 1)

#define LOONGGPU_CMD_XDMA_BODY_NR	8

#define WRITE_DST_SEL(x)                ((x) << 8)
                /* 0 - register
		 * 1 - memory
		 */
#define WRITE_WAIT                      (1 << 15)
#endif  /* #ifdef _LOONGGPU_TEST_H_ */
