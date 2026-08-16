# Copyright © 2017-2018 Intel Corporation

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.


datadir_amdgpu = join_paths(get_option('prefix'), get_option('datadir'), 'libdrm')

libdrm_amdgpu = library(
  'drm_amdgpu',
  [
    files(
      'amdgpu_asic_id.c', 'amdgpu_bo.c', 'amdgpu_cs.c', 'amdgpu_device.c',
      'amdgpu_gpu_info.c', 'amdgpu_vamgr.c', 'amdgpu_vm.c', 'handle_table.c',
      'amdgpu_userq.c',
    ),
    config_file,
  ],
  c_args : [
    libdrm_c_args,
    '-DAMDGPU_ASIC_ID_TABLE="@0@"'.format(join_paths(datadir_amdgpu, 'amdgpu.ids')),
  ],
  include_directories : [inc_root, inc_drm],
  link_with : libdrm,
  dependencies : [dep_threads, dep_atomic_ops, dep_rt],
  version : '1.@0@.0'.format(patch_ver),
  install : true,
)

install_headers('amdgpu.h', subdir : 'libdrm')

pkg.generate(
  libdrm_amdgpu,
  name : 'libdrm_amdgpu',
  subdirs : ['.', 'libdrm'],
  description : 'Userspace interface to kernel DRM services for amdgpu',
)

ext_libdrm_amdgpu = declare_dependency(
  link_with : [libdrm, libdrm_amdgpu],
  include_directories : [inc_drm, include_directories('.')],
)

meson.override_dependency('libdrm_amdgpu', ext_libdrm_amdgpu)

test(
  'amdgpu-symbols-check',
  symbols_check,
  args : [
    '--lib', libdrm_amdgpu,
    '--symbols-file', files('amdgpu-symbols.txt'),
    '--nm', prog_nm.full_path(),
  ],
)

loonggpu_defsym_args = [
  '-Wl,--defsym=loonggpu_bo_alloc=amdgpu_bo_alloc',
  '-Wl,--defsym=loonggpu_bo_cpu_map=amdgpu_bo_cpu_map',
  '-Wl,--defsym=loonggpu_bo_cpu_unmap=amdgpu_bo_cpu_unmap',
  '-Wl,--defsym=loonggpu_bo_export=amdgpu_bo_export',
  '-Wl,--defsym=loonggpu_bo_free=amdgpu_bo_free',
  '-Wl,--defsym=loonggpu_bo_import=amdgpu_bo_import',
  '-Wl,--defsym=loonggpu_bo_list_create=amdgpu_bo_list_create',
  '-Wl,--defsym=loonggpu_bo_list_destroy=amdgpu_bo_list_destroy',
  '-Wl,--defsym=loonggpu_bo_list_update=amdgpu_bo_list_update',
  '-Wl,--defsym=loonggpu_bo_query_info=amdgpu_bo_query_info',
  '-Wl,--defsym=loonggpu_bo_set_metadata=amdgpu_bo_set_metadata',
  '-Wl,--defsym=loonggpu_bo_va_op=amdgpu_bo_va_op',
  '-Wl,--defsym=loonggpu_bo_va_op_raw=amdgpu_bo_va_op_raw',
  '-Wl,--defsym=loonggpu_bo_wait_for_idle=amdgpu_bo_wait_for_idle',
  '-Wl,--defsym=loonggpu_create_bo_from_user_mem=amdgpu_create_bo_from_user_mem',
  '-Wl,--defsym=loonggpu_cs_chunk_fence_info_to_data=amdgpu_cs_chunk_fence_info_to_data',
  '-Wl,--defsym=loonggpu_cs_chunk_fence_to_dep=amdgpu_cs_chunk_fence_to_dep',
  '-Wl,--defsym=loonggpu_cs_create_semaphore=amdgpu_cs_create_semaphore',
  '-Wl,--defsym=loonggpu_cs_create_syncobj=amdgpu_cs_create_syncobj',
  '-Wl,--defsym=loonggpu_cs_create_syncobj2=amdgpu_cs_create_syncobj2',
  '-Wl,--defsym=loonggpu_cs_ctx_create=amdgpu_cs_ctx_create',
  '-Wl,--defsym=loonggpu_cs_ctx_create2=amdgpu_cs_ctx_create2',
  '-Wl,--defsym=loonggpu_cs_ctx_free=amdgpu_cs_ctx_free',
  '-Wl,--defsym=loonggpu_cs_destroy_semaphore=amdgpu_cs_destroy_semaphore',
  '-Wl,--defsym=loonggpu_cs_destroy_syncobj=amdgpu_cs_destroy_syncobj',
  '-Wl,--defsym=loonggpu_cs_export_syncobj=amdgpu_cs_export_syncobj',
  '-Wl,--defsym=loonggpu_cs_fence_to_handle=amdgpu_cs_fence_to_handle',
  '-Wl,--defsym=loonggpu_cs_import_syncobj=amdgpu_cs_import_syncobj',
  '-Wl,--defsym=loonggpu_cs_query_fence_status=amdgpu_cs_query_fence_status',
  '-Wl,--defsym=loonggpu_cs_query_reset_state=amdgpu_cs_query_reset_state',
  '-Wl,--defsym=loonggpu_cs_signal_semaphore=amdgpu_cs_signal_semaphore',
  '-Wl,--defsym=loonggpu_cs_submit=amdgpu_cs_submit',
  '-Wl,--defsym=loonggpu_cs_submit_raw=amdgpu_cs_submit_raw',
  '-Wl,--defsym=loonggpu_cs_submit_raw2=amdgpu_cs_submit_raw2',
  '-Wl,--defsym=loonggpu_cs_syncobj_export_sync_file=amdgpu_cs_syncobj_export_sync_file',
  '-Wl,--defsym=loonggpu_cs_syncobj_export_sync_file2=amdgpu_cs_syncobj_export_sync_file2',
  '-Wl,--defsym=loonggpu_cs_syncobj_import_sync_file=amdgpu_cs_syncobj_import_sync_file',
  '-Wl,--defsym=loonggpu_cs_syncobj_import_sync_file2=amdgpu_cs_syncobj_import_sync_file2',
  '-Wl,--defsym=loonggpu_cs_syncobj_query=amdgpu_cs_syncobj_query',
  '-Wl,--defsym=loonggpu_cs_syncobj_query2=amdgpu_cs_syncobj_query2',
  '-Wl,--defsym=loonggpu_cs_syncobj_reset=amdgpu_cs_syncobj_reset',
  '-Wl,--defsym=loonggpu_cs_syncobj_signal=amdgpu_cs_syncobj_signal',
  '-Wl,--defsym=loonggpu_cs_syncobj_timeline_signal=amdgpu_cs_syncobj_timeline_signal',
  '-Wl,--defsym=loonggpu_cs_syncobj_timeline_wait=amdgpu_cs_syncobj_timeline_wait',
  '-Wl,--defsym=loonggpu_cs_syncobj_transfer=amdgpu_cs_syncobj_transfer',
  '-Wl,--defsym=loonggpu_cs_syncobj_wait=amdgpu_cs_syncobj_wait',
  '-Wl,--defsym=loonggpu_cs_wait_fences=amdgpu_cs_wait_fences',
  '-Wl,--defsym=loonggpu_cs_wait_semaphore=amdgpu_cs_wait_semaphore',
  '-Wl,--defsym=loonggpu_device_deinitialize=amdgpu_device_deinitialize',
  '-Wl,--defsym=loonggpu_device_get_fd=amdgpu_device_get_fd',
  '-Wl,--defsym=loonggpu_device_initialize=amdgpu_device_initialize',
  '-Wl,--defsym=loonggpu_get_marketing_name=amdgpu_get_marketing_name',
  '-Wl,--defsym=loonggpu_query_buffer_size_alignment=amdgpu_query_buffer_size_alignment',
  '-Wl,--defsym=loonggpu_query_crtc_from_id=amdgpu_query_crtc_from_id',
  '-Wl,--defsym=loonggpu_query_firmware_version=amdgpu_query_firmware_version',
  '-Wl,--defsym=loonggpu_query_gds_info=amdgpu_query_gds_info',
  '-Wl,--defsym=loonggpu_query_gpu_info=amdgpu_query_gpu_info',
  '-Wl,--defsym=loonggpu_query_heap_info=amdgpu_query_heap_info',
  '-Wl,--defsym=loonggpu_query_hw_ip_count=amdgpu_query_hw_ip_count',
  '-Wl,--defsym=loonggpu_query_hw_ip_info=amdgpu_query_hw_ip_info',
  '-Wl,--defsym=loonggpu_query_info=amdgpu_query_info',
  '-Wl,--defsym=loonggpu_query_sensor_info=amdgpu_query_sensor_info',
  '-Wl,--defsym=loonggpu_query_sw_info=amdgpu_query_sw_info',
  '-Wl,--defsym=loonggpu_read_mm_registers=amdgpu_read_mm_registers',
  '-Wl,--defsym=loonggpu_va_range_alloc=amdgpu_va_range_alloc',
  '-Wl,--defsym=loonggpu_va_range_free=amdgpu_va_range_free',
  '-Wl,--defsym=loonggpu_va_range_query=amdgpu_va_range_query',
  '-Wl,--defsym=loonggpu_vm_reserve_vmid=amdgpu_vm_reserve_vmid',
  '-Wl,--defsym=loonggpu_vm_unreserve_vmid=amdgpu_vm_unreserve_vmid',
]

libdrm_loonggpu = library(
  'drm_loonggpu',
  [
    files(
      'amdgpu_asic_id.c', 'amdgpu_bo.c', 'amdgpu_cs.c', 'amdgpu_device.c',
      'amdgpu_gpu_info.c', 'amdgpu_vamgr.c', 'amdgpu_vm.c', 'handle_table.c',
      'amdgpu_userq.c',
    ),
    config_file,
  ],
  c_args : [
    libdrm_c_args,
    '-DAMDGPU_ASIC_ID_TABLE="@0@"'.format(join_paths(datadir_amdgpu, 'loonggpu.ids')),
    '-DLOONGGPU',
  ],
  include_directories : [inc_root, inc_drm],
  link_with : libdrm,
  dependencies : [dep_threads, dep_atomic_ops, dep_rt],
  link_args : loonggpu_defsym_args + ['-Wl,--version-script=' + join_paths(meson.current_source_dir(), 'loonggpu.map')],
  version : '1.@0@.0'.format(patch_ver),
  install : true,
)
