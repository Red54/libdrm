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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <sys/ioctl.h>
#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#endif

#include "xf86drm.h"
#include "gsgpu_drm.h"
#include "gsgpu_internal.h"

static int gsgpu_cs_unreference_sem(gsgpu_semaphore_handle sem);
static int gsgpu_cs_reset_sem(gsgpu_semaphore_handle sem);

/**
 * Create command submission context
 *
 * \param   dev      - \c [in] Device handle. See #gsgpu_device_initialize()
 * \param   priority - \c [in] Context creation flags. See GSGPU_CTX_PRIORITY_*
 * \param   context  - \c [out] GPU Context handle
 *
 * \return  0 on success otherwise POSIX Error code
*/
int gsgpu_cs_ctx_create2(gsgpu_device_handle dev, uint32_t priority,
							gsgpu_context_handle *context)
{
	struct gsgpu_context *gpu_context;
	union drm_gsgpu_ctx args;
	int i, j, k;
	int r;

	if (!dev || !context)
		return -EINVAL;

	gpu_context = calloc(1, sizeof(struct gsgpu_context));
	if (!gpu_context)
		return -ENOMEM;

	gpu_context->dev = dev;

	r = pthread_mutex_init(&gpu_context->sequence_mutex, NULL);
	if (r)
		goto error;

	/* Create the context */
	memset(&args, 0, sizeof(args));
	args.in.op = GSGPU_CTX_OP_ALLOC_CTX;
	args.in.priority = priority;

	r = drmCommandWriteRead(dev->fd, DRM_GSGPU_CTX, &args, sizeof(args));
	if (r)
		goto error;

	gpu_context->id = args.out.alloc.ctx_id;
	for (i = 0; i < GSGPU_HW_IP_NUM; i++)
		for (j = 0; j < GSGPU_HW_IP_INSTANCE_MAX_COUNT; j++)
			for (k = 0; k < GSGPU_CS_MAX_RINGS; k++)
				list_inithead(&gpu_context->sem_list[i][j][k]);
	*context = (gsgpu_context_handle)gpu_context;

	return 0;

error:
	pthread_mutex_destroy(&gpu_context->sequence_mutex);
	free(gpu_context);
	return r;
}

int gsgpu_cs_ctx_create(gsgpu_device_handle dev,
			 gsgpu_context_handle *context)
{
	return gsgpu_cs_ctx_create2(dev, GSGPU_CTX_PRIORITY_NORMAL, context);
}

/**
 * Release command submission context
 *
 * \param   dev - \c [in] gsgpu device handle
 * \param   context - \c [in] gsgpu context handle
 *
 * \return  0 on success otherwise POSIX Error code
*/
int gsgpu_cs_ctx_free(gsgpu_context_handle context)
{
	union drm_gsgpu_ctx args;
	int i, j, k;
	int r;

	if (!context)
		return -EINVAL;

	pthread_mutex_destroy(&context->sequence_mutex);

	/* now deal with kernel side */
	memset(&args, 0, sizeof(args));
	args.in.op = GSGPU_CTX_OP_FREE_CTX;
	args.in.ctx_id = context->id;
	r = drmCommandWriteRead(context->dev->fd, DRM_GSGPU_CTX,
				&args, sizeof(args));
	for (i = 0; i < GSGPU_HW_IP_NUM; i++) {
		for (j = 0; j < GSGPU_HW_IP_INSTANCE_MAX_COUNT; j++) {
			for (k = 0; k < GSGPU_CS_MAX_RINGS; k++) {
				gsgpu_semaphore_handle sem;
				LIST_FOR_EACH_ENTRY(sem, &context->sem_list[i][j][k], list) {
					list_del(&sem->list);
					gsgpu_cs_reset_sem(sem);
					gsgpu_cs_unreference_sem(sem);
				}
			}
		}
	}
	free(context);

	return r;
}

int gsgpu_cs_query_reset_state(gsgpu_context_handle context,
				uint32_t *state, uint32_t *hangs)
{
	union drm_gsgpu_ctx args;
	int r;

	if (!context)
		return -EINVAL;

	memset(&args, 0, sizeof(args));
	args.in.op = GSGPU_CTX_OP_QUERY_STATE;
	args.in.ctx_id = context->id;
	r = drmCommandWriteRead(context->dev->fd, DRM_GSGPU_CTX,
				&args, sizeof(args));
	if (!r) {
		*state = args.out.state.reset_status;
		*hangs = args.out.state.hangs;
	}
	return r;
}

/**
 * Submit command to kernel DRM
 * \param   dev - \c [in]  Device handle
 * \param   context - \c [in]  GPU Context
 * \param   ibs_request - \c [in]  Pointer to submission requests
 * \param   fence - \c [out] return fence for this submission
 *
 * \return  0 on success otherwise POSIX Error code
 * \sa gsgpu_cs_submit()
*/
static int gsgpu_cs_submit_one(gsgpu_context_handle context,
				struct gsgpu_cs_request *ibs_request)
{
	union drm_gsgpu_cs cs;
	uint64_t *chunk_array;
	struct drm_gsgpu_cs_chunk *chunks;
	struct drm_gsgpu_cs_chunk_data *chunk_data;
	struct drm_gsgpu_cs_chunk_dep *dependencies = NULL;
	struct drm_gsgpu_cs_chunk_dep *sem_dependencies = NULL;
	struct list_head *sem_list;
	gsgpu_semaphore_handle sem, tmp;
	uint32_t i, size, sem_count = 0;
	bool user_fence;
	int r = 0;

	if (ibs_request->ip_type >= GSGPU_HW_IP_NUM)
		return -EINVAL;
	if (ibs_request->ring >= GSGPU_CS_MAX_RINGS)
		return -EINVAL;
	if (ibs_request->number_of_ibs > GSGPU_CS_MAX_IBS_PER_SUBMIT)
		return -EINVAL;
	if (ibs_request->number_of_ibs == 0) {
		ibs_request->seq_no = GSGPU_NULL_SUBMIT_SEQ;
		return 0;
	}
	user_fence = (ibs_request->fence_info.handle != NULL);

	size = ibs_request->number_of_ibs + (user_fence ? 2 : 1) + 1;

	chunk_array = alloca(sizeof(uint64_t) * size);
	chunks = alloca(sizeof(struct drm_gsgpu_cs_chunk) * size);

	size = ibs_request->number_of_ibs + (user_fence ? 1 : 0);

	chunk_data = alloca(sizeof(struct drm_gsgpu_cs_chunk_data) * size);

	memset(&cs, 0, sizeof(cs));
	cs.in.chunks = (uint64_t)(uintptr_t)chunk_array;
	cs.in.ctx_id = context->id;
	if (ibs_request->resources)
		cs.in.bo_list_handle = ibs_request->resources->handle;
	cs.in.num_chunks = ibs_request->number_of_ibs;
	/* IB chunks */
	for (i = 0; i < ibs_request->number_of_ibs; i++) {
		struct gsgpu_cs_ib_info *ib;
		chunk_array[i] = (uint64_t)(uintptr_t)&chunks[i];
		chunks[i].chunk_id = GSGPU_CHUNK_ID_IB;
		chunks[i].length_dw = sizeof(struct drm_gsgpu_cs_chunk_ib) / 4;
		chunks[i].chunk_data = (uint64_t)(uintptr_t)&chunk_data[i];

		ib = &ibs_request->ibs[i];

		chunk_data[i].ib_data._pad = 0;
		chunk_data[i].ib_data.va_start = ib->ib_mc_address;
		chunk_data[i].ib_data.ib_bytes = ib->size * 4;
		chunk_data[i].ib_data.ip_type = ibs_request->ip_type;
		chunk_data[i].ib_data.ip_instance = ibs_request->ip_instance;
		chunk_data[i].ib_data.ring = ibs_request->ring;
		chunk_data[i].ib_data.flags = ib->flags;
	}

	pthread_mutex_lock(&context->sequence_mutex);

	if (user_fence) {
		i = cs.in.num_chunks++;

		/* fence chunk */
		chunk_array[i] = (uint64_t)(uintptr_t)&chunks[i];
		chunks[i].chunk_id = GSGPU_CHUNK_ID_FENCE;
		chunks[i].length_dw = sizeof(struct drm_gsgpu_cs_chunk_fence) / 4;
		chunks[i].chunk_data = (uint64_t)(uintptr_t)&chunk_data[i];

		/* fence bo handle */
		chunk_data[i].fence_data.handle = ibs_request->fence_info.handle->handle;
		/* offset */
		chunk_data[i].fence_data.offset =
			ibs_request->fence_info.offset * sizeof(uint64_t);
	}

	if (ibs_request->number_of_dependencies) {
		dependencies = malloc(sizeof(struct drm_gsgpu_cs_chunk_dep) *
			ibs_request->number_of_dependencies);
		if (!dependencies) {
			r = -ENOMEM;
			goto error_unlock;
		}

		for (i = 0; i < ibs_request->number_of_dependencies; ++i) {
			struct gsgpu_cs_fence *info = &ibs_request->dependencies[i];
			struct drm_gsgpu_cs_chunk_dep *dep = &dependencies[i];
			dep->ip_type = info->ip_type;
			dep->ip_instance = info->ip_instance;
			dep->ring = info->ring;
			dep->ctx_id = info->context->id;
			dep->handle = info->fence;
		}

		i = cs.in.num_chunks++;

		/* dependencies chunk */
		chunk_array[i] = (uint64_t)(uintptr_t)&chunks[i];
		chunks[i].chunk_id = GSGPU_CHUNK_ID_DEPENDENCIES;
		chunks[i].length_dw = sizeof(struct drm_gsgpu_cs_chunk_dep) / 4
			* ibs_request->number_of_dependencies;
		chunks[i].chunk_data = (uint64_t)(uintptr_t)dependencies;
	}

	sem_list = &context->sem_list[ibs_request->ip_type][ibs_request->ip_instance][ibs_request->ring];
	LIST_FOR_EACH_ENTRY(sem, sem_list, list)
		sem_count++;
	if (sem_count) {
		sem_dependencies = malloc(sizeof(struct drm_gsgpu_cs_chunk_dep) * sem_count);
		if (!sem_dependencies) {
			r = -ENOMEM;
			goto error_unlock;
		}
		sem_count = 0;
		LIST_FOR_EACH_ENTRY_SAFE(sem, tmp, sem_list, list) {
			struct gsgpu_cs_fence *info = &sem->signal_fence;
			struct drm_gsgpu_cs_chunk_dep *dep = &sem_dependencies[sem_count++];
			dep->ip_type = info->ip_type;
			dep->ip_instance = info->ip_instance;
			dep->ring = info->ring;
			dep->ctx_id = info->context->id;
			dep->handle = info->fence;

			list_del(&sem->list);
			gsgpu_cs_reset_sem(sem);
			gsgpu_cs_unreference_sem(sem);
		}
		i = cs.in.num_chunks++;

		/* dependencies chunk */
		chunk_array[i] = (uint64_t)(uintptr_t)&chunks[i];
		chunks[i].chunk_id = GSGPU_CHUNK_ID_DEPENDENCIES;
		chunks[i].length_dw = sizeof(struct drm_gsgpu_cs_chunk_dep) / 4 * sem_count;
		chunks[i].chunk_data = (uint64_t)(uintptr_t)sem_dependencies;
	}

	r = drmCommandWriteRead(context->dev->fd, DRM_GSGPU_CS,
				&cs, sizeof(cs));
	if (r)
		goto error_unlock;

	ibs_request->seq_no = cs.out.handle;
	context->last_seq[ibs_request->ip_type][ibs_request->ip_instance][ibs_request->ring] = ibs_request->seq_no;
error_unlock:
	pthread_mutex_unlock(&context->sequence_mutex);
	free(dependencies);
	free(sem_dependencies);
	return r;
}

int gsgpu_cs_submit(gsgpu_context_handle context,
		     uint64_t flags,
		     struct gsgpu_cs_request *ibs_request,
		     uint32_t number_of_requests)
{
	uint32_t i;
	int r;

	if (!context || !ibs_request)
		return -EINVAL;

	r = 0;
	for (i = 0; i < number_of_requests; i++) {
		r = gsgpu_cs_submit_one(context, ibs_request);
		if (r)
			break;
		ibs_request++;
	}

	return r;
}

/**
 * Calculate absolute timeout.
 *
 * \param   timeout - \c [in] timeout in nanoseconds.
 *
 * \return  absolute timeout in nanoseconds
*/
drm_private uint64_t gsgpu_cs_calculate_timeout(uint64_t timeout)
{
	int r;

	if (timeout != GSGPU_TIMEOUT_INFINITE) {
		struct timespec current;
		uint64_t current_ns;
		r = clock_gettime(CLOCK_MONOTONIC, &current);
		if (r) {
			fprintf(stderr, "clock_gettime() returned error (%d)!", errno);
			return GSGPU_TIMEOUT_INFINITE;
		}

		current_ns = ((uint64_t)current.tv_sec) * 1000000000ull;
		current_ns += current.tv_nsec;
		timeout += current_ns;
		if (timeout < current_ns)
			timeout = GSGPU_TIMEOUT_INFINITE;
	}
	return timeout;
}

static int gsgpu_ioctl_wait_cs(gsgpu_context_handle context,
				unsigned ip,
				unsigned ip_instance,
				uint32_t ring,
				uint64_t handle,
				uint64_t timeout_ns,
				uint64_t flags,
				bool *busy)
{
	gsgpu_device_handle dev = context->dev;
	union drm_gsgpu_wait_cs args;
	int r;

	memset(&args, 0, sizeof(args));
	args.in.handle = handle;
	args.in.ip_type = ip;
	args.in.ip_instance = ip_instance;
	args.in.ring = ring;
	args.in.ctx_id = context->id;

	if (flags & GSGPU_QUERY_FENCE_TIMEOUT_IS_ABSOLUTE)
		args.in.timeout = timeout_ns;
	else
		args.in.timeout = gsgpu_cs_calculate_timeout(timeout_ns);

	r = drmIoctl(dev->fd, DRM_IOCTL_GSGPU_WAIT_CS, &args);
	if (r)
		return -errno;

	*busy = args.out.status;
	return 0;
}

int gsgpu_cs_query_fence_status(struct gsgpu_cs_fence *fence,
				 uint64_t timeout_ns,
				 uint64_t flags,
				 uint32_t *expired)
{
	bool busy = true;
	int r;

	if (!fence || !expired || !fence->context)
		return -EINVAL;
	if (fence->ip_type >= GSGPU_HW_IP_NUM)
		return -EINVAL;
	if (fence->ring >= GSGPU_CS_MAX_RINGS)
		return -EINVAL;
	if (fence->fence == GSGPU_NULL_SUBMIT_SEQ) {
		*expired = true;
		return 0;
	}

	*expired = false;

	r = gsgpu_ioctl_wait_cs(fence->context,
				fence->ip_type,fence->ip_instance,
				fence->ring,fence->fence,
				timeout_ns, flags, &busy);

	if (!r && !busy)
		*expired = true;

	return r;
}

static int gsgpu_ioctl_wait_fences(struct gsgpu_cs_fence *fences,
				    uint32_t fence_count,
				    bool wait_all,
				    uint64_t timeout_ns,
				    uint32_t *status,
				    uint32_t *first)
{
	struct drm_gsgpu_fence *drm_fences;
	gsgpu_device_handle dev = fences[0].context->dev;
	union drm_gsgpu_wait_fences args;
	int r;
	uint32_t i;

	drm_fences = alloca(sizeof(struct drm_gsgpu_fence) * fence_count);
	for (i = 0; i < fence_count; i++) {
		drm_fences[i].ctx_id = fences[i].context->id;
		drm_fences[i].ip_type = fences[i].ip_type;
		drm_fences[i].ip_instance = fences[i].ip_instance;
		drm_fences[i].ring = fences[i].ring;
		drm_fences[i].seq_no = fences[i].fence;
	}

	memset(&args, 0, sizeof(args));
	args.in.fences = (uint64_t)(uintptr_t)drm_fences;
	args.in.fence_count = fence_count;
	args.in.wait_all = wait_all;
	args.in.timeout_ns = gsgpu_cs_calculate_timeout(timeout_ns);

	r = drmIoctl(dev->fd, DRM_IOCTL_GSGPU_WAIT_FENCES, &args);
	if (r)
		return -errno;

	*status = args.out.status;

	if (first)
		*first = args.out.first_signaled;

	return 0;
}

int gsgpu_cs_wait_fences(struct gsgpu_cs_fence *fences,
			  uint32_t fence_count,
			  bool wait_all,
			  uint64_t timeout_ns,
			  uint32_t *status,
			  uint32_t *first)
{
	uint32_t i;

	/* Sanity check */
	if (!fences || !status || !fence_count)
		return -EINVAL;

	for (i = 0; i < fence_count; i++) {
		if (NULL == fences[i].context)
			return -EINVAL;
		if (fences[i].ip_type >= GSGPU_HW_IP_NUM)
			return -EINVAL;
		if (fences[i].ring >= GSGPU_CS_MAX_RINGS)
			return -EINVAL;
	}

	*status = 0;

	return gsgpu_ioctl_wait_fences(fences, fence_count, wait_all,
					timeout_ns, status, first);
}

int gsgpu_cs_create_semaphore(gsgpu_semaphore_handle *sem)
{
	struct gsgpu_semaphore *gpu_semaphore;

	if (!sem)
		return -EINVAL;

	gpu_semaphore = calloc(1, sizeof(struct gsgpu_semaphore));
	if (!gpu_semaphore)
		return -ENOMEM;

	atomic_set(&gpu_semaphore->refcount, 1);
	*sem = gpu_semaphore;

	return 0;
}

int gsgpu_cs_signal_semaphore(gsgpu_context_handle ctx,
			       uint32_t ip_type,
			       uint32_t ip_instance,
			       uint32_t ring,
			       gsgpu_semaphore_handle sem)
{
	if (!ctx || !sem)
		return -EINVAL;
	if (ip_type >= GSGPU_HW_IP_NUM)
		return -EINVAL;
	if (ring >= GSGPU_CS_MAX_RINGS)
		return -EINVAL;
	/* sem has been signaled */
	if (sem->signal_fence.context)
		return -EINVAL;
	pthread_mutex_lock(&ctx->sequence_mutex);
	sem->signal_fence.context = ctx;
	sem->signal_fence.ip_type = ip_type;
	sem->signal_fence.ip_instance = ip_instance;
	sem->signal_fence.ring = ring;
	sem->signal_fence.fence = ctx->last_seq[ip_type][ip_instance][ring];
	update_references(NULL, &sem->refcount);
	pthread_mutex_unlock(&ctx->sequence_mutex);
	return 0;
}

int gsgpu_cs_wait_semaphore(gsgpu_context_handle ctx,
			     uint32_t ip_type,
			     uint32_t ip_instance,
			     uint32_t ring,
			     gsgpu_semaphore_handle sem)
{
	if (!ctx || !sem)
		return -EINVAL;
	if (ip_type >= GSGPU_HW_IP_NUM)
		return -EINVAL;
	if (ring >= GSGPU_CS_MAX_RINGS)
		return -EINVAL;
	/* must signal first */
	if (!sem->signal_fence.context)
		return -EINVAL;

	pthread_mutex_lock(&ctx->sequence_mutex);
	list_add(&sem->list, &ctx->sem_list[ip_type][ip_instance][ring]);
	pthread_mutex_unlock(&ctx->sequence_mutex);
	return 0;
}

static int gsgpu_cs_reset_sem(gsgpu_semaphore_handle sem)
{
	if (!sem || !sem->signal_fence.context)
		return -EINVAL;

	sem->signal_fence.context = NULL;
	sem->signal_fence.ip_type = 0;
	sem->signal_fence.ip_instance = 0;
	sem->signal_fence.ring = 0;
	sem->signal_fence.fence = 0;

	return 0;
}

static int gsgpu_cs_unreference_sem(gsgpu_semaphore_handle sem)
{
	if (!sem)
		return -EINVAL;

	if (update_references(&sem->refcount, NULL))
		free(sem);
	return 0;
}

int gsgpu_cs_destroy_semaphore(gsgpu_semaphore_handle sem)
{
	return gsgpu_cs_unreference_sem(sem);
}

int gsgpu_cs_create_syncobj2(gsgpu_device_handle dev,
			      uint32_t  flags,
			      uint32_t *handle)
{
	if (NULL == dev)
		return -EINVAL;

	return drmSyncobjCreate(dev->fd, flags, handle);
}

int gsgpu_cs_create_syncobj(gsgpu_device_handle dev,
			     uint32_t *handle)
{
	if (NULL == dev)
		return -EINVAL;

	return drmSyncobjCreate(dev->fd, 0, handle);
}

int gsgpu_cs_destroy_syncobj(gsgpu_device_handle dev,
			      uint32_t handle)
{
	if (NULL == dev)
		return -EINVAL;

	return drmSyncobjDestroy(dev->fd, handle);
}

int gsgpu_cs_syncobj_reset(gsgpu_device_handle dev,
			    const uint32_t *syncobjs, uint32_t syncobj_count)
{
	if (NULL == dev)
		return -EINVAL;

	return drmSyncobjReset(dev->fd, syncobjs, syncobj_count);
}

int gsgpu_cs_syncobj_signal(gsgpu_device_handle dev,
			     const uint32_t *syncobjs, uint32_t syncobj_count)
{
	if (NULL == dev)
		return -EINVAL;

	return drmSyncobjSignal(dev->fd, syncobjs, syncobj_count);
}

int gsgpu_cs_syncobj_wait(gsgpu_device_handle dev,
			   uint32_t *handles, unsigned num_handles,
			   int64_t timeout_nsec, unsigned flags,
			   uint32_t *first_signaled)
{
	if (NULL == dev)
		return -EINVAL;

	return drmSyncobjWait(dev->fd, handles, num_handles, timeout_nsec,
			      flags, first_signaled);
}

int gsgpu_cs_export_syncobj(gsgpu_device_handle dev,
			     uint32_t handle,
			     int *shared_fd)
{
	if (NULL == dev)
		return -EINVAL;

	return drmSyncobjHandleToFD(dev->fd, handle, shared_fd);
}

int gsgpu_cs_import_syncobj(gsgpu_device_handle dev,
			     int shared_fd,
			     uint32_t *handle)
{
	if (NULL == dev)
		return -EINVAL;

	return drmSyncobjFDToHandle(dev->fd, shared_fd, handle);
}

int gsgpu_cs_syncobj_export_sync_file(gsgpu_device_handle dev,
				       uint32_t syncobj,
				       int *sync_file_fd)
{
	if (NULL == dev)
		return -EINVAL;

	return drmSyncobjExportSyncFile(dev->fd, syncobj, sync_file_fd);
}

int gsgpu_cs_syncobj_import_sync_file(gsgpu_device_handle dev,
				       uint32_t syncobj,
				       int sync_file_fd)
{
	if (NULL == dev)
		return -EINVAL;

	return drmSyncobjImportSyncFile(dev->fd, syncobj, sync_file_fd);
}

int gsgpu_cs_submit_raw(gsgpu_device_handle dev,
			 gsgpu_context_handle context,
			 gsgpu_bo_list_handle bo_list_handle,
			 int num_chunks,
			 struct drm_gsgpu_cs_chunk *chunks,
			 uint64_t *seq_no)
{
	union drm_gsgpu_cs cs = {0};
	uint64_t *chunk_array;
	int i, r;
	if (num_chunks == 0)
		return -EINVAL;

	chunk_array = alloca(sizeof(uint64_t) * num_chunks);
	for (i = 0; i < num_chunks; i++)
		chunk_array[i] = (uint64_t)(uintptr_t)&chunks[i];
	cs.in.chunks = (uint64_t)(uintptr_t)chunk_array;
	cs.in.ctx_id = context->id;
	cs.in.bo_list_handle = bo_list_handle ? bo_list_handle->handle : 0;
	cs.in.num_chunks = num_chunks;
	r = drmCommandWriteRead(dev->fd, DRM_GSGPU_CS,
				&cs, sizeof(cs));
	if (r)
		return r;

	if (seq_no)
		*seq_no = cs.out.handle;
	return 0;
}

void gsgpu_cs_chunk_fence_info_to_data(struct gsgpu_cs_fence_info *fence_info,
					struct drm_gsgpu_cs_chunk_data *data)
{
	data->fence_data.handle = fence_info->handle->handle;
	data->fence_data.offset = fence_info->offset * sizeof(uint64_t);
}

void gsgpu_cs_chunk_fence_to_dep(struct gsgpu_cs_fence *fence,
				  struct drm_gsgpu_cs_chunk_dep *dep)
{
	dep->ip_type = fence->ip_type;
	dep->ip_instance = fence->ip_instance;
	dep->ring = fence->ring;
	dep->ctx_id = fence->context->id;
	dep->handle = fence->fence;
}

int gsgpu_cs_fence_to_handle(gsgpu_device_handle dev,
			      struct gsgpu_cs_fence *fence,
			      uint32_t what,
			      uint32_t *out_handle)
{
	union drm_gsgpu_fence_to_handle fth = {0};
	int r;

	fth.in.fence.ctx_id = fence->context->id;
	fth.in.fence.ip_type = fence->ip_type;
	fth.in.fence.ip_instance = fence->ip_instance;
	fth.in.fence.ring = fence->ring;
	fth.in.fence.seq_no = fence->fence;
	fth.in.what = what;

	r = drmCommandWriteRead(dev->fd, DRM_GSGPU_FENCE_TO_HANDLE,
				&fth, sizeof(fth));
	if (r == 0)
		*out_handle = fth.out.handle;
	return r;
}
