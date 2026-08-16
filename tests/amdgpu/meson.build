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

amdgpu_stress = executable(
  'amdgpu_stress',
  files(
    'amdgpu_stress.c'
  ),
  dependencies : [dep_threads, dep_atomic_ops],
  include_directories : [inc_root, inc_drm, include_directories('../../amdgpu')],
  link_with : [libdrm, libdrm_amdgpu],
  install : with_install_tests,
)

loonggpu_stress = executable(
  'loonggpu_stress',
  files(
    'amdgpu_stress.c'
  ),
  c_args : [
    '-DLOONGGPU',
    '-Damdgpu_device_initialize=loonggpu_device_initialize',
    '-Damdgpu_bo_alloc=loonggpu_bo_alloc',
    '-Damdgpu_bo_cpu_map=loonggpu_bo_cpu_map',
    '-Damdgpu_bo_list_create=loonggpu_bo_list_create',
    '-Damdgpu_bo_list_destroy=loonggpu_bo_list_destroy',
    '-Damdgpu_bo_va_op_raw=loonggpu_bo_va_op_raw',
    '-Damdgpu_cs_ctx_create=loonggpu_cs_ctx_create',
    '-Damdgpu_cs_query_fence_status=loonggpu_cs_query_fence_status',
    '-Damdgpu_cs_submit=loonggpu_cs_submit',
    '-Damdgpu_va_range_alloc=loonggpu_va_range_alloc',
  ],
  dependencies : [dep_threads, dep_atomic_ops],
  include_directories : [inc_root, inc_drm, include_directories('../../amdgpu')],
  link_with : [libdrm, libdrm_loonggpu],
  install : with_install_tests,
)
