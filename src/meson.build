# this file is part of szl.
#
# Copyright (c) 2016, 2017 Dima Krasner
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

ext_dir = join_paths(get_option('prefix'), get_option('libdir'), 'szl')

cflags = ['-std=gnu99', '-D_GNU_SOURCE', '-DSZL_VERSION="@0@"'.format(meson.project_version()), '-DSZL_EXT_DIR="@0@"'.format(ext_dir), '-Wall', '-pedantic', '-Wno-overlength-strings', '-Wformat', '-Wformat-security']
cc = meson.get_compiler('c')
if cc.has_argument('-Wformat-signedness')
    cflags += ['-Wformat-signedness']
endif

if get_option('use_int')
	cflags += ['-DSZL_USE_INT']
endif

if not get_option('with_float')
	cflags += ['-DSZL_NO_FLOAT']
endif

if get_option('use_float')
	cflags += ['-DSZL_USE_FLOAT']
endif

if not get_option('with_unicode')
	cflags += ['-DSZL_NO_UNICODE']
endif

builtin_all = get_option('builtin_all')
if builtin_all
	with_dl = false
else
	with_dl = get_option('with_dl')
endif

dl_dep = cc.find_library('dl', required: false)
if not dl_dep.found()
	with_dl = false
endif

if not with_dl
	cflags += ['-DSZL_NO_DL']
endif
add_project_arguments(cflags, language: 'c')

doc_dir = join_paths(get_option('datadir'), 'doc', 'szl')

builtin_exts = ['obj', 'proc', 'exec', 'str', 'exc', 'socket', 'null', 'logic', 'file', 'dir', 'io', 'list', 'math', 'loop', 'ext', 'time', 'env', 'path', 'signal', 'poll', 'timer', 'syscall', 'dict', 'szl']
exts = []

with_tls = get_option('with_tls')
if with_tls != 'no'
	tls_ext_deps = dependency('openssl', required: false)
	if tls_ext_deps.found()
		if builtin_all or with_tls == 'builtin'
			builtin_exts += 'tls'
		else
			exts += 'tls'
		endif
	endif
endif

with_zlib = get_option('with_zlib')
if with_zlib != 'no'
	zlib_ext_deps = dependency('zlib', required: false)
	if zlib_ext_deps.found()
		if builtin_all or with_zlib == 'builtin'
			builtin_exts += 'zlib'
		else
			exts += 'zlib'
		endif
	endif
endif

with_ffi = get_option('with_ffi')
if with_ffi != 'no'
	libffi_dep = dependency('libffi', required: false)
	if libffi_dep.found()
		if with_dl
			ffi_ext_deps = [dl_dep, libffi_dep]
		else
			ffi_ext_deps = [libffi_dep]
		endif
		if builtin_all or with_ffi == 'builtin'
			builtin_exts += 'ffi'
		else
			exts += 'ffi'
		endif
	endif
endif

with_curl = get_option('with_curl')
if with_curl != 'no'
	curl_ext_deps = dependency('libcurl', required: false)
	if curl_ext_deps.found()
		if builtin_all or with_curl == 'builtin'
			builtin_exts += 'curl'
		else
			exts += 'curl'
		endif
	endif
endif

with_archive = get_option('with_archive')
if with_archive != 'no'
	archive_ext_deps = dependency('libarchive', required: false)
	if archive_ext_deps.found()
		if builtin_all or with_archive == 'builtin'
			builtin_exts += 'archive'
		else
			exts += 'archive'
		endif
	endif
endif

linenoise_ext_deps = declare_dependency(sources: join_paths('linenoise', 'linenoise.c'))
if builtin_all or get_option('with_linenoise') == 'builtin'
	builtin_exts += 'linenoise'
else
	exts += 'linenoise'
endif
install_data(join_paths('linenoise', 'LICENSE'),
             install_dir: join_paths(doc_dir, 'linenoise'))

with_ed25519 = get_option('with_ed25519')
if with_ed25519 != 'no'
	ed25519_ext_deps = declare_dependency(sources: ['ed25519/src/add_scalar.c', 'ed25519/src/fe.c', 'ed25519/src/ge.c', 'ed25519/src/key_exchange.c', 'ed25519/src/keypair.c', 'ed25519/src/sc.c', 'ed25519/src/seed.c', 'ed25519/src/sha512.c', 'ed25519/src/sign.c', 'ed25519/src/verify.c'])
	if builtin_all or with_ed25519 == 'builtin'
		builtin_exts += 'ed25519'
	else
		exts += 'ed25519'
	endif
	install_data(join_paths('ed25519', 'license.txt'),
	             install_dir: join_paths(doc_dir, 'ed25519'))
endif

with_lzfse = get_option('with_lzfse')
if with_lzfse != 'no'
	if cc.has_argument('-Wno-pedantic')
		lzfse_cflags = ['-Wno-unknown-pragmas', '-Wno-unused-variable', '-Wno-pedantic']
	else
		lzfse_cflags = ['-Wno-unknown-pragmas', '-Wno-unused-variable']
	endif

	lzfse_ext_deps = declare_dependency(sources: ['lzfse/src/lzfse_decode_base.c', 'lzfse/src/lzfse_decode.c', 'lzfse/src/lzfse_encode_base.c', 'lzfse/src/lzfse_encode.c', 'lzfse/src/lzfse_fse.c', 'lzfse/src/lzvn_decode_base.c', 'lzfse/src/lzvn_encode_base.c'],
	                                    compile_args: lzfse_cflags)
	if builtin_all or with_lzfse == 'builtin'
		builtin_exts += 'lzfse'
	else
		exts += 'lzfse'
	endif
	install_data(join_paths('lzfse', 'LICENSE'),
	             install_dir: join_paths(doc_dir, 'lzfse'))
endif

with_zstd = get_option('with_zstd')
if with_zstd != 'no'
	zstd_ext_deps = declare_dependency(include_directories: include_directories('zstd/lib/common', 'zstd/lib'),
	                                   sources: ['zstd/lib/common/threading.c', 'zstd/lib/common/zstd_common.c', 'zstd/lib/common/entropy_common.c', 'zstd/lib/common/error_private.c', 'zstd/lib/common/xxhash.c', 'zstd/lib/common/fse_decompress.c', 'zstd/lib/common/pool.c', 'zstd/lib/compress/huf_compress.c', 'zstd/lib/compress/zstd_compress.c', 'zstd/lib/compress/fse_compress.c', 'zstd/lib/compress/zstdmt_compress.c', 'zstd/lib/decompress/zstd_decompress.c', 'zstd/lib/decompress/huf_decompress.c'])
	if builtin_all or with_zstd == 'builtin'
		builtin_exts += 'zstd'
	else
		exts += 'zstd'
	endif
	install_data(join_paths('zstd', 'LICENSE'), join_paths('zstd', 'PATENTS'),
	             install_dir: join_paths(doc_dir, 'zstd'))
endif

foreach ext: ['test', 'oop', 'server', 'resp', 'http', 'https']
	if get_option('with_@0@'.format(ext)) != 'no'
		if builtin_all or get_option('with_@0@'.format(ext)) == 'builtin'
			builtin_exts += ext
		else
			exts += ext
		endif
	endif
endforeach

foreach ext: builtin_exts + exts
	ext_srcs = '@0@_ext_srcs'.format(ext)

	if not is_variable(ext_srcs)
		ext_szl = 'szl_@0@.szl'.format(ext)
		ext_h = 'szl_@0@.h'.format(ext)

		if run_command('[', '-f', join_paths(meson.current_source_dir(), ext_szl), ']').returncode() == 0
			ext_h_target = custom_target('@0@_h_gen'.format(ext),
			                             output: ext_h,
			                             command: ['sh', join_paths(meson.current_source_dir(), 'minify.sh'), join_paths(meson.current_source_dir(), ext_szl)],
			                             input: ext_szl,
			                             capture: true)
			deps_var = '@0@_ext_deps'.format(ext)
			if is_variable(deps_var)
				set_variable(deps_var, get_variable(deps_var) + [declare_dependency(sources: ext_h_target)])
			else
				set_variable(deps_var, [declare_dependency(sources: ext_h_target)])
			endif
		endif

		ext_c = 'szl_@0@.c'.format(ext)
		if run_command('[', '-f', join_paths(meson.current_source_dir(), ext_c), ']').returncode() == 0
			set_variable(ext_srcs, ext_c)
		else
			ext_cfg = configuration_data()
			ext_cfg.set('name', ext)
			ext_cfg.set('src', ext_h)
			set_variable(ext_srcs, configure_file(input: 'szl_wrapper.c.in',
			                                      output: ext_c,
			                                      configuration: ext_cfg))
		endif
	endif
endforeach

libszl_srcs = ['libszl.c']
libszl_deps = []

builtin_c = custom_target('builtin',
                          output: 'szl_builtin.c',
                          command: ['sh', join_paths(meson.current_source_dir(), 'mkbuiltin.sh')] + builtin_exts,
                          capture: true)

foreach builtin: builtin_exts
	libszl_srcs += get_variable('@0@_ext_srcs'.format(builtin))

	deps_var = '@0@_ext_deps'.format(builtin)
	if is_variable(deps_var)
		libszl_deps += get_variable(deps_var)
	endif
endforeach

if with_dl
	libszl_deps += [dl_dep]
endif

if builtin_all
	libszl = static_library('szl',
	                        libszl_srcs + [builtin_c],
	                        dependencies: libszl_deps + [cc.find_library('m', required: true)],
	                        install: true)
else
	libszl = shared_library('szl',
	                        libszl_srcs + [builtin_c],
	                        dependencies: libszl_deps + [cc.find_library('m', required: true)],
	                        install: true)
endif
install_headers('szl.h')

foreach ext: exts
	shared_library('szl_@0@'.format(ext),
	               get_variable('@0@_ext_srcs'.format(ext)),
	               link_with: libszl,
	               dependencies: get_variable('@0@_ext_deps'.format(ext)),
	               name_prefix: '',
	               install: true,
	               install_dir: ext_dir)
endforeach

executable('szl', 'szl.c', link_with: libszl, install: true)
executable('szlsh', 'szlsh.c', link_with: libszl, install: true)
