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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "libdrm_macros.h"
#include "xf86drm.h"
#include "loonggpu_drm.h"
#include "loonggpu_internal.h"
#include "util_hash_table.h"
#include "util_math.h"

static void loonggpu_close_kms_handle(loonggpu_device_handle dev,
				     uint32_t handle)
{
	struct drm_gem_close args = {};

	args.handle = handle;
	drmIoctl(dev->fd, DRM_IOCTL_GEM_CLOSE, &args);
}

int loonggpu_bo_alloc(loonggpu_device_handle dev,
		    struct loonggpu_bo_alloc_request *alloc_buffer,
		    loonggpu_bo_handle *buf_handle)
{
	struct loonggpu_bo *bo;
	union drm_loonggpu_gem_create args;
	unsigned heap = alloc_buffer->preferred_heap;
	int r = 0;

	/* It's an error if the heap is not specified */
	if (!(heap & (LOONGGPU_GEM_DOMAIN_GTT | LOONGGPU_GEM_DOMAIN_VRAM)))
		return -EINVAL;

	bo = calloc(1, sizeof(struct loonggpu_bo));
	if (!bo)
		return -ENOMEM;

	atomic_set(&bo->refcount, 1);
	bo->dev = dev;
	bo->alloc_size = alloc_buffer->alloc_size;

	memset(&args, 0, sizeof(args));
	args.in.bo_size = alloc_buffer->alloc_size;
	args.in.alignment = alloc_buffer->phys_alignment;

	/* Set the placement. */
	args.in.domains = heap;
	args.in.domain_flags = alloc_buffer->flags;

	/* Allocate the buffer with the preferred heap. */
	r = drmCommandWriteRead(dev->fd, DRM_LOONGGPU_GEM_CREATE,
				&args, sizeof(args));
	if (r) {
		free(bo);
		return r;
	}

	bo->handle = args.out.handle;

	pthread_mutex_init(&bo->cpu_access_mutex, NULL);

	*buf_handle = bo;
	return 0;
}

int loonggpu_bo_set_metadata(loonggpu_bo_handle bo,
			   struct loonggpu_bo_metadata *info)
{
	struct drm_loonggpu_gem_metadata args = {};

	args.handle = bo->handle;
	args.op = LOONGGPU_GEM_METADATA_OP_SET_METADATA;
	args.data.flags = info->flags;
	args.data.tiling_info = info->tiling_info;

	if (info->size_metadata > sizeof(args.data.data))
		return -EINVAL;

	if (info->size_metadata) {
		args.data.data_size_bytes = info->size_metadata;
		memcpy(args.data.data, info->umd_metadata, info->size_metadata);
	}

	return drmCommandWriteRead(bo->dev->fd,
				   DRM_LOONGGPU_GEM_METADATA,
				   &args, sizeof(args));
}

int loonggpu_bo_query_info(loonggpu_bo_handle bo,
			 struct loonggpu_bo_info *info)
{
	struct drm_loonggpu_gem_metadata metadata = {};
	struct drm_loonggpu_gem_create_in bo_info = {};
	struct drm_loonggpu_gem_op gem_op = {};
	int r;

	/* Validate the BO passed in */
	if (!bo->handle)
		return -EINVAL;

	/* Query metadata. */
	metadata.handle = bo->handle;
	metadata.op = LOONGGPU_GEM_METADATA_OP_GET_METADATA;

	r = drmCommandWriteRead(bo->dev->fd, DRM_LOONGGPU_GEM_METADATA,
				&metadata, sizeof(metadata));
	if (r)
		return r;

	if (metadata.data.data_size_bytes >
	    sizeof(info->metadata.umd_metadata))
		return -EINVAL;

	/* Query buffer info. */
	gem_op.handle = bo->handle;
	gem_op.op = LOONGGPU_GEM_OP_GET_GEM_CREATE_INFO;
	gem_op.value = (uintptr_t)&bo_info;

	r = drmCommandWriteRead(bo->dev->fd, DRM_LOONGGPU_GEM_OP,
				&gem_op, sizeof(gem_op));
	if (r)
		return r;

	memset(info, 0, sizeof(*info));
	info->alloc_size = bo_info.bo_size;
	info->phys_alignment = bo_info.alignment;
	info->preferred_heap = bo_info.domains;
	info->alloc_flags = bo_info.domain_flags;
	info->metadata.flags = metadata.data.flags;
	info->metadata.tiling_info = metadata.data.tiling_info;

	info->metadata.size_metadata = metadata.data.data_size_bytes;
	if (metadata.data.data_size_bytes > 0)
		memcpy(info->metadata.umd_metadata, metadata.data.data,
		       metadata.data.data_size_bytes);

	return 0;
}

static void loonggpu_add_handle_to_table(loonggpu_bo_handle bo)
{
	pthread_mutex_lock(&bo->dev->bo_table_mutex);
	util_hash_table_set(bo->dev->bo_handles,
			    (void*)(uintptr_t)bo->handle, bo);
	pthread_mutex_unlock(&bo->dev->bo_table_mutex);
}

static int loonggpu_bo_export_flink(loonggpu_bo_handle bo)
{
	struct drm_gem_flink flink;
	int fd, dma_fd;
	uint32_t handle;
	int r;

	fd = bo->dev->fd;
	handle = bo->handle;
	if (bo->flink_name)
		return 0;


	if (bo->dev->flink_fd != bo->dev->fd) {
		r = drmPrimeHandleToFD(bo->dev->fd, bo->handle, DRM_CLOEXEC,
				       &dma_fd);
		if (!r) {
			r = drmPrimeFDToHandle(bo->dev->flink_fd, dma_fd, &handle);
			close(dma_fd);
		}
		if (r)
			return r;
		fd = bo->dev->flink_fd;
	}
	memset(&flink, 0, sizeof(flink));
	flink.handle = handle;

	r = drmIoctl(fd, DRM_IOCTL_GEM_FLINK, &flink);
	if (r)
		return r;

	bo->flink_name = flink.name;

	if (bo->dev->flink_fd != bo->dev->fd) {
		struct drm_gem_close args = {};
		args.handle = handle;
		drmIoctl(bo->dev->flink_fd, DRM_IOCTL_GEM_CLOSE, &args);
	}

	pthread_mutex_lock(&bo->dev->bo_table_mutex);
	util_hash_table_set(bo->dev->bo_flink_names,
			    (void*)(uintptr_t)bo->flink_name,
			    bo);
	pthread_mutex_unlock(&bo->dev->bo_table_mutex);

	return 0;
}

int loonggpu_bo_export(loonggpu_bo_handle bo,
		     enum loonggpu_bo_handle_type type,
		     uint32_t *shared_handle)
{
	int r;

	switch (type) {
	case loonggpu_bo_handle_type_gem_flink_name:
		r = loonggpu_bo_export_flink(bo);
		if (r)
			return r;

		*shared_handle = bo->flink_name;
		return 0;

	case loonggpu_bo_handle_type_kms:
		loonggpu_add_handle_to_table(bo);
		*shared_handle = bo->handle;
		return 0;

	case loonggpu_bo_handle_type_dma_buf_fd:
		loonggpu_add_handle_to_table(bo);
		return drmPrimeHandleToFD(bo->dev->fd, bo->handle, DRM_CLOEXEC,
				       (int*)shared_handle);
	}
	return -EINVAL;
}

int loonggpu_bo_import(loonggpu_device_handle dev,
		     enum loonggpu_bo_handle_type type,
		     uint32_t shared_handle,
		     struct loonggpu_bo_import_result *output)
{
	struct drm_gem_open open_arg = {};
	struct loonggpu_bo *bo = NULL;
	int r;
	int dma_fd;
	uint64_t dma_buf_size = 0;

	/* We must maintain a list of pairs <handle, bo>, so that we always
	 * return the same loonggpu_bo instance for the same handle. */
	pthread_mutex_lock(&dev->bo_table_mutex);

	/* Convert a DMA buf handle to a KMS handle now. */
	if (type == loonggpu_bo_handle_type_dma_buf_fd) {
		uint32_t handle;
		off_t size;

		/* Get a KMS handle. */
		r = drmPrimeFDToHandle(dev->fd, shared_handle, &handle);
		if (r) {
			pthread_mutex_unlock(&dev->bo_table_mutex);
			return r;
		}

		/* Query the buffer size. */
		size = lseek(shared_handle, 0, SEEK_END);
		if (size == (off_t)-1) {
			pthread_mutex_unlock(&dev->bo_table_mutex);
			loonggpu_close_kms_handle(dev, handle);
			return -errno;
		}
		lseek(shared_handle, 0, SEEK_SET);

		dma_buf_size = size;
		shared_handle = handle;
	}

	/* If we have already created a buffer with this handle, find it. */
	switch (type) {
	case loonggpu_bo_handle_type_gem_flink_name:
		bo = util_hash_table_get(dev->bo_flink_names,
					 (void*)(uintptr_t)shared_handle);
		break;

	case loonggpu_bo_handle_type_dma_buf_fd:
		bo = util_hash_table_get(dev->bo_handles,
					 (void*)(uintptr_t)shared_handle);
		break;

	case loonggpu_bo_handle_type_kms:
		/* Importing a KMS handle in not allowed. */
		pthread_mutex_unlock(&dev->bo_table_mutex);
		return -EPERM;

	default:
		pthread_mutex_unlock(&dev->bo_table_mutex);
		return -EINVAL;
	}

	if (bo) {
		/* The buffer already exists, just bump the refcount. */
		atomic_inc(&bo->refcount);
		pthread_mutex_unlock(&dev->bo_table_mutex);

		output->buf_handle = bo;
		output->alloc_size = bo->alloc_size;
		return 0;
	}

	bo = calloc(1, sizeof(struct loonggpu_bo));
	if (!bo) {
		pthread_mutex_unlock(&dev->bo_table_mutex);
		if (type == loonggpu_bo_handle_type_dma_buf_fd) {
			loonggpu_close_kms_handle(dev, shared_handle);
		}
		return -ENOMEM;
	}

	/* Open the handle. */
	switch (type) {
	case loonggpu_bo_handle_type_gem_flink_name:
		open_arg.name = shared_handle;
		r = drmIoctl(dev->flink_fd, DRM_IOCTL_GEM_OPEN, &open_arg);
		if (r) {
			free(bo);
			pthread_mutex_unlock(&dev->bo_table_mutex);
			return r;
		}

		bo->handle = open_arg.handle;
		if (dev->flink_fd != dev->fd) {
			r = drmPrimeHandleToFD(dev->flink_fd, bo->handle, DRM_CLOEXEC, &dma_fd);
			if (r) {
				free(bo);
				pthread_mutex_unlock(&dev->bo_table_mutex);
				return r;
			}
			r = drmPrimeFDToHandle(dev->fd, dma_fd, &bo->handle );

			close(dma_fd);

			if (r) {
				free(bo);
				pthread_mutex_unlock(&dev->bo_table_mutex);
				return r;
			}
		}
		bo->flink_name = shared_handle;
		bo->alloc_size = open_arg.size;
		util_hash_table_set(dev->bo_flink_names,
				    (void*)(uintptr_t)bo->flink_name, bo);
		break;

	case loonggpu_bo_handle_type_dma_buf_fd:
		bo->handle = shared_handle;
		bo->alloc_size = dma_buf_size;
		break;

	case loonggpu_bo_handle_type_kms:
		assert(0); /* unreachable */
	}

	/* Initialize it. */
	atomic_set(&bo->refcount, 1);
	bo->dev = dev;
	pthread_mutex_init(&bo->cpu_access_mutex, NULL);

	util_hash_table_set(dev->bo_handles, (void*)(uintptr_t)bo->handle, bo);
	pthread_mutex_unlock(&dev->bo_table_mutex);

	output->buf_handle = bo;
	output->alloc_size = bo->alloc_size;
	return 0;
}

int loonggpu_bo_free(loonggpu_bo_handle buf_handle)
{
	struct loonggpu_device *dev;
	struct loonggpu_bo *bo = buf_handle;

	assert(bo != NULL);
	dev = bo->dev;
	pthread_mutex_lock(&dev->bo_table_mutex);

	if (update_references(&bo->refcount, NULL)) {
		/* Remove the buffer from the hash tables. */
		util_hash_table_remove(dev->bo_handles,
					(void*)(uintptr_t)bo->handle);

		if (bo->flink_name) {
			util_hash_table_remove(dev->bo_flink_names,
						(void*)(uintptr_t)bo->flink_name);
		}

		/* Release CPU access. */
		if (bo->cpu_map_count > 0) {
			bo->cpu_map_count = 1;
			loonggpu_bo_cpu_unmap(bo);
		}

		loonggpu_close_kms_handle(dev, bo->handle);
		pthread_mutex_destroy(&bo->cpu_access_mutex);
		free(bo);
	}

	pthread_mutex_unlock(&dev->bo_table_mutex);
	return 0;
}

int loonggpu_bo_cpu_map(loonggpu_bo_handle bo, void **cpu)
{
	union drm_loonggpu_gem_mmap args;
	void *ptr;
	int r;

	pthread_mutex_lock(&bo->cpu_access_mutex);

	if (bo->cpu_ptr) {
		/* already mapped */
		assert(bo->cpu_map_count > 0);
		bo->cpu_map_count++;
		*cpu = bo->cpu_ptr;
		pthread_mutex_unlock(&bo->cpu_access_mutex);
		return 0;
	}

	assert(bo->cpu_map_count == 0);

	memset(&args, 0, sizeof(args));

	/* Query the buffer address (args.addr_ptr).
	 * The kernel driver ignores the offset and size parameters. */
	args.in.handle = bo->handle;

	r = drmCommandWriteRead(bo->dev->fd, DRM_LOONGGPU_GEM_MMAP, &args,
				sizeof(args));
	if (r) {
		pthread_mutex_unlock(&bo->cpu_access_mutex);
		return r;
	}

	/* Map the buffer. */
	ptr = drm_mmap(NULL, bo->alloc_size, PROT_READ | PROT_WRITE, MAP_SHARED,
		       bo->dev->fd, args.out.addr_ptr);
	if (ptr == MAP_FAILED) {
		pthread_mutex_unlock(&bo->cpu_access_mutex);
		return -errno;
	}

	bo->cpu_ptr = ptr;
	bo->cpu_map_count = 1;
	pthread_mutex_unlock(&bo->cpu_access_mutex);

	*cpu = ptr;
	return 0;
}

int loonggpu_bo_cpu_unmap(loonggpu_bo_handle bo)
{
	int r;

	pthread_mutex_lock(&bo->cpu_access_mutex);
	assert(bo->cpu_map_count >= 0);

	if (bo->cpu_map_count == 0) {
		/* not mapped */
		pthread_mutex_unlock(&bo->cpu_access_mutex);
		return -EINVAL;
	}

	bo->cpu_map_count--;
	if (bo->cpu_map_count > 0) {
		/* mapped multiple times */
		pthread_mutex_unlock(&bo->cpu_access_mutex);
		return 0;
	}

	r = drm_munmap(bo->cpu_ptr, bo->alloc_size) == 0 ? 0 : -errno;
	bo->cpu_ptr = NULL;
	pthread_mutex_unlock(&bo->cpu_access_mutex);
	return r;
}

int loonggpu_query_buffer_size_alignment(loonggpu_device_handle dev,
				struct loonggpu_buffer_size_alignments *info)
{
	info->size_local = dev->dev_info.pte_fragment_size;
	info->size_remote = dev->dev_info.gart_page_size;
	return 0;
}

int loonggpu_bo_wait_for_idle(loonggpu_bo_handle bo,
			    uint64_t timeout_ns,
			    bool *busy)
{
	union drm_loonggpu_gem_wait_idle args;
	int r;

	memset(&args, 0, sizeof(args));
	args.in.handle = bo->handle;
	args.in.timeout = loonggpu_cs_calculate_timeout(timeout_ns);

	r = drmCommandWriteRead(bo->dev->fd, DRM_LOONGGPU_GEM_WAIT_IDLE,
				&args, sizeof(args));

	if (r == 0) {
		*busy = args.out.status;
		return 0;
	} else {
		fprintf(stderr, "loonggpu: GEM_WAIT_IDLE failed with %i\n", r);
		return r;
	}
}

int loonggpu_create_bo_from_user_mem(loonggpu_device_handle dev,
				    void *cpu,
				    uint64_t size,
				    loonggpu_bo_handle *buf_handle)
{
	int r;
	struct loonggpu_bo *bo;
	struct drm_loonggpu_gem_userptr args;

	args.addr = (uintptr_t)cpu;
	args.flags = LOONGGPU_GEM_USERPTR_ANONONLY | LOONGGPU_GEM_USERPTR_REGISTER |
		LOONGGPU_GEM_USERPTR_VALIDATE;
	args.size = size;
	r = drmCommandWriteRead(dev->fd, DRM_LOONGGPU_GEM_USERPTR,
				&args, sizeof(args));
	if (r)
		return r;

	bo = calloc(1, sizeof(struct loonggpu_bo));
	if (!bo)
		return -ENOMEM;

	atomic_set(&bo->refcount, 1);
	bo->dev = dev;
	bo->alloc_size = size;
	bo->handle = args.handle;

	*buf_handle = bo;

	return r;
}

int loonggpu_bo_list_create(loonggpu_device_handle dev,
			  uint32_t number_of_resources,
			  loonggpu_bo_handle *resources,
			  uint8_t *resource_prios,
			  loonggpu_bo_list_handle *result)
{
	struct drm_loonggpu_bo_list_entry *list;
	union drm_loonggpu_bo_list args;
	unsigned i;
	int r;

	if (!number_of_resources)
		return -EINVAL;

	/* overflow check for multiplication */
	if (number_of_resources > UINT32_MAX / sizeof(struct drm_loonggpu_bo_list_entry))
		return -EINVAL;

	list = malloc(number_of_resources * sizeof(struct drm_loonggpu_bo_list_entry));
	if (!list)
		return -ENOMEM;

	*result = malloc(sizeof(struct loonggpu_bo_list));
	if (!*result) {
		free(list);
		return -ENOMEM;
	}

	memset(&args, 0, sizeof(args));
	args.in.operation = LOONGGPU_BO_LIST_OP_CREATE;
	args.in.bo_number = number_of_resources;
	args.in.bo_info_size = sizeof(struct drm_loonggpu_bo_list_entry);
	args.in.bo_info_ptr = (uint64_t)(uintptr_t)list;

	for (i = 0; i < number_of_resources; i++) {
		list[i].bo_handle = resources[i]->handle;
		if (resource_prios)
			list[i].bo_priority = resource_prios[i];
		else
			list[i].bo_priority = 0;
	}

	r = drmCommandWriteRead(dev->fd, DRM_LOONGGPU_BO_LIST,
				&args, sizeof(args));
	free(list);
	if (r) {
		free(*result);
		return r;
	}

	(*result)->dev = dev;
	(*result)->handle = args.out.list_handle;
	return 0;
}

int loonggpu_bo_list_destroy(loonggpu_bo_list_handle list)
{
	union drm_loonggpu_bo_list args;
	int r;

	memset(&args, 0, sizeof(args));
	args.in.operation = LOONGGPU_BO_LIST_OP_DESTROY;
	args.in.list_handle = list->handle;

	r = drmCommandWriteRead(list->dev->fd, DRM_LOONGGPU_BO_LIST,
				&args, sizeof(args));

	if (!r)
		free(list);

	return r;
}

int loonggpu_bo_list_update(loonggpu_bo_list_handle handle,
			  uint32_t number_of_resources,
			  loonggpu_bo_handle *resources,
			  uint8_t *resource_prios)
{
	struct drm_loonggpu_bo_list_entry *list;
	union drm_loonggpu_bo_list args;
	unsigned i;
	int r;

	if (!number_of_resources)
		return -EINVAL;

	/* overflow check for multiplication */
	if (number_of_resources > UINT32_MAX / sizeof(struct drm_loonggpu_bo_list_entry))
		return -EINVAL;

	list = malloc(number_of_resources * sizeof(struct drm_loonggpu_bo_list_entry));
	if (!list)
		return -ENOMEM;

	args.in.operation = LOONGGPU_BO_LIST_OP_UPDATE;
	args.in.list_handle = handle->handle;
	args.in.bo_number = number_of_resources;
	args.in.bo_info_size = sizeof(struct drm_loonggpu_bo_list_entry);
	args.in.bo_info_ptr = (uintptr_t)list;

	for (i = 0; i < number_of_resources; i++) {
		list[i].bo_handle = resources[i]->handle;
		if (resource_prios)
			list[i].bo_priority = resource_prios[i];
		else
			list[i].bo_priority = 0;
	}

	r = drmCommandWriteRead(handle->dev->fd, DRM_LOONGGPU_BO_LIST,
				&args, sizeof(args));
	free(list);
	return r;
}

int loonggpu_bo_va_op(loonggpu_bo_handle bo,
		     uint64_t offset,
		     uint64_t size,
		     uint64_t addr,
		     uint64_t flags,
		     uint32_t ops)
{
	loonggpu_device_handle dev = bo->dev;

	size = ALIGN(size, getpagesize());

	return loonggpu_bo_va_op_raw(dev, bo, offset, size, addr,
				   LOONGGPU_VM_PAGE_READABLE |
				   LOONGGPU_VM_PAGE_WRITEABLE |
				   LOONGGPU_VM_PAGE_EXECUTABLE, ops);
}

int loonggpu_bo_va_op_raw(loonggpu_device_handle dev,
			loonggpu_bo_handle bo,
			uint64_t offset,
			uint64_t size,
			uint64_t addr,
			uint64_t flags,
			uint32_t ops)
{
	struct drm_loonggpu_gem_va va;
	int r;

	if (ops != LOONGGPU_VA_OP_MAP && ops != LOONGGPU_VA_OP_UNMAP &&
	    ops != LOONGGPU_VA_OP_REPLACE && ops != LOONGGPU_VA_OP_CLEAR)
		return -EINVAL;

	memset(&va, 0, sizeof(va));
	va.handle = bo ? bo->handle : 0;
	va.operation = ops;
	va.flags = flags;
	va.va_address = addr;
	va.offset_in_bo = offset;
	va.map_size = size;

	r = drmCommandWriteRead(dev->fd, DRM_LOONGGPU_GEM_VA, &va, sizeof(va));

	return r;
}
