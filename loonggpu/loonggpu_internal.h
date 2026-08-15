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

#ifndef _LOONGGPU_INTERNAL_H_
#define _LOONGGPU_INTERNAL_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <pthread.h>

#include "libdrm_macros.h"
#include "xf86atomic.h"
#include "loonggpu.h"
#include "util_double_list.h"

#define LOONGGPU_CS_MAX_RINGS 8
/* do not use below macro if b is not power of 2 aligned value */
#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define ROUND_UP(x, y) ((((x)-1) | __round_mask(x, y))+1)
#define ROUND_DOWN(x, y) ((x) & ~__round_mask(x, y))

#define LOONGGPU_INVALID_VA_ADDRESS	0xffffffffffffffff
#define LOONGGPU_NULL_SUBMIT_SEQ		0

struct loonggpu_bo_va_hole {
	struct list_head list;
	uint64_t offset;
	uint64_t size;
};

struct loonggpu_bo_va_mgr {
	uint64_t va_max;
	struct list_head va_holes;
	pthread_mutex_t bo_va_mutex;
	uint32_t va_alignment;
};

struct loonggpu_va {
	loonggpu_device_handle dev;
	uint64_t address;
	uint64_t size;
	enum loonggpu_gpu_va_range range;
	struct loonggpu_bo_va_mgr *vamgr;
};

struct loonggpu_device {
	atomic_t refcount;
	int fd;
	int flink_fd;
	unsigned major_version;
	unsigned minor_version;

	char *marketing_name;
	/** List of buffer handles. Protected by bo_table_mutex. */
	struct util_hash_table *bo_handles;
	/** List of buffer GEM flink names. Protected by bo_table_mutex. */
	struct util_hash_table *bo_flink_names;
	/** This protects all hash tables. */
	pthread_mutex_t bo_table_mutex;
	struct drm_loonggpu_info_device dev_info;
	struct loonggpu_gpu_info info;
	/** The VA manager for the lower virtual address space */
	struct loonggpu_bo_va_mgr vamgr;
	/** The VA manager for the 32bit address space */
	struct loonggpu_bo_va_mgr vamgr_32;
	/** The VA manager for the high virtual address space */
	struct loonggpu_bo_va_mgr vamgr_high;
	/** The VA manager for the 32bit high address space */
	struct loonggpu_bo_va_mgr vamgr_high_32;
};

struct loonggpu_bo {
	atomic_t refcount;
	struct loonggpu_device *dev;

	uint64_t alloc_size;

	uint32_t handle;
	uint32_t flink_name;

	pthread_mutex_t cpu_access_mutex;
	void *cpu_ptr;
	int cpu_map_count;
};

struct loonggpu_bo_list {
	struct loonggpu_device *dev;

	uint32_t handle;
};

struct loonggpu_context {
	struct loonggpu_device *dev;
	/** Mutex for accessing fences and to maintain command submissions
	    in good sequence. */
	pthread_mutex_t sequence_mutex;
	/* context id*/
	uint32_t id;
	uint64_t last_seq[LOONGGPU_HW_IP_NUM][LOONGGPU_HW_IP_INSTANCE_MAX_COUNT][LOONGGPU_CS_MAX_RINGS];
	struct list_head sem_list[LOONGGPU_HW_IP_NUM][LOONGGPU_HW_IP_INSTANCE_MAX_COUNT][LOONGGPU_CS_MAX_RINGS];
};

/**
 * Structure describing sw semaphore based on scheduler
 *
 */
struct loonggpu_semaphore {
	atomic_t refcount;
	struct list_head list;
	struct loonggpu_cs_fence signal_fence;
};

/**
 * Functions.
 */

drm_private void loonggpu_vamgr_init(struct loonggpu_bo_va_mgr *mgr, uint64_t start,
		       uint64_t max, uint64_t alignment);

drm_private void loonggpu_vamgr_deinit(struct loonggpu_bo_va_mgr *mgr);

drm_private void loonggpu_parse_asic_ids(struct loonggpu_device *dev);

drm_private int loonggpu_query_gpu_info_init(loonggpu_device_handle dev);

drm_private uint64_t loonggpu_cs_calculate_timeout(uint64_t timeout);

/**
 * Inline functions.
 */

/**
 * Increment src and decrement dst as if we were updating references
 * for an assignment between 2 pointers of some objects.
 *
 * \return  true if dst is 0
 */
static inline bool update_references(atomic_t *dst, atomic_t *src)
{
	if (dst != src) {
		/* bump src first */
		if (src) {
			assert(atomic_read(src) > 0);
			atomic_inc(src);
		}
		if (dst) {
			assert(atomic_read(dst) > 0);
			return atomic_dec_and_test(dst);
		}
	}
	return false;
}

#endif
