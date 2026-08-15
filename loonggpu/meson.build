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


datadir_loonggpu = join_paths(get_option('prefix'), get_option('datadir'), 'libdrm')

libdrm_loonggpu = shared_library(
  'drm_loonggpu',
  [
    files(
      'loonggpu_asic_id.c', 'loonggpu_bo.c', 'loonggpu_cs.c', 'loonggpu_device.c',
      'loonggpu_gpu_info.c', 'loonggpu_vamgr.c', 'loonggpu_vm.c', 'util_hash.c',
      'util_hash_table.c', 'loonggpu_hw_sema.c',
    ),
    config_file,
  ],
  c_args : [
    warn_c_args,
    '-DLOONGGPU_ASIC_ID_TABLE="@0@"'.format(join_paths(datadir_loonggpu, 'loonggpu.ids')),
  ],
  include_directories : [inc_root, inc_drm],
  link_with : libdrm,
  dependencies : dep_pthread_stubs,
  version : '1.0.0',
  install : true,
)

install_headers('loonggpu.h', subdir : 'libdrm')

pkg.generate(
  name : 'libdrm_loonggpu',
  libraries : libdrm_loonggpu,
  subdirs : ['.', 'libdrm'],
  version : meson.project_version(),
  requires_private : 'libdrm',
  description : 'Userspace interface to kernel DRM services for loonggpu',
)

ext_libdrm_loonggpu = declare_dependency(
  link_with : [libdrm, libdrm_loonggpu],
  include_directories : [inc_drm, include_directories('.')],
)

test(
  'loonggpu-symbol-check',
  prog_bash,
  env : env_test,
  args : [files('loonggpu-symbol-check'), libdrm_loonggpu]
)
