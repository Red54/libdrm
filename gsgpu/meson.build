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


datadir_gsgpu = join_paths(get_option('prefix'), get_option('datadir'), 'libdrm')

libdrm_gsgpu = shared_library(
  'drm_gsgpu',
  [
    files(
      'gsgpu_asic_id.c', 'gsgpu_bo.c', 'gsgpu_cs.c', 'gsgpu_device.c',
      'gsgpu_gpu_info.c', 'gsgpu_vamgr.c', 'gsgpu_vm.c', 'util_hash.c',
      'util_hash_table.c', 'gsgpu_hw_sema.c',
    ),
    config_file,
  ],
  c_args : [
    warn_c_args,
    '-DGSGPU_ASIC_ID_TABLE="@0@"'.format(join_paths(datadir_gsgpu, 'gsgpu.ids')),
  ],
  include_directories : [inc_root, inc_drm],
  link_with : libdrm,
  dependencies : dep_pthread_stubs,
  version : '1.0.0',
  install : true,
)

install_headers('gsgpu.h', subdir : 'libdrm')

pkg.generate(
  name : 'libdrm_gsgpu',
  libraries : libdrm_gsgpu,
  subdirs : ['.', 'libdrm'],
  version : meson.project_version(),
  requires_private : 'libdrm',
  description : 'Userspace interface to kernel DRM services for gsgpu',
)

ext_libdrm_gsgpu = declare_dependency(
  link_with : [libdrm, libdrm_gsgpu],
  include_directories : [inc_drm, include_directories('.')],
)

test(
  'gsgpu-symbol-check',
  prog_bash,
  env : env_test,
  args : [files('gsgpu-symbol-check'), libdrm_gsgpu]
)
