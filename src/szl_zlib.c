/*
 * this file is part of szl.
 *
 * Copyright (c) 2016, 2017 Dima Krasner
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdlib.h>

#include <zlib.h>

#include "szl.h"

#define WBITS_GZIP (MAX_WBITS | 16)
/* use small 64K chunks if no size was specified during decompression, to reduce
 * memory consumption */
#define DEF_DECOMPRESS_BUFSIZ (64 * 1024)

static
enum szl_res szl_zlib_proc_crc32(struct szl_interp *interp,
                                 const unsigned int objc,
                                 struct szl_obj **objv)
{
	char *s;
	szl_int init;
	size_t len;

	if (objc == 2)
		init = (szl_int)crc32(0L, Z_NULL, 0);
	else if (!szl_as_int(interp, objv[2], &init))
		return SZL_ERR;

	if (!szl_as_str(interp, objv[1], &s, &len) || !len)
		return SZL_ERR;

	return szl_set_last_int(interp,
	                        (szl_int)(crc32((uLong)init,
	                                         (const Bytef *)s,
	                                         (uInt)len) & 0xFFFFFFFF));
}

static
int szl_zlib_compress(struct szl_interp *interp,
                      const char *in,
                      const size_t len,
                      const szl_int level,
                      const int wbits)
{
	z_stream strm = {0};
	Bytef *buf;
	struct szl_obj *obj;

	if ((level != Z_DEFAULT_COMPRESSION) &&
	    ((level < Z_NO_COMPRESSION) || (level > Z_BEST_COMPRESSION))) {
		szl_set_last_str(interp, "level must be 0 to 9", -1);
		return SZL_ERR;
	}

	if (deflateInit2(&strm,
	                 (int)level,
	                 Z_DEFLATED,
	                 wbits,
	                 MAX_MEM_LEVEL,
	                 Z_DEFAULT_STRATEGY) != Z_OK)
		return SZL_ERR;

	strm.avail_out = deflateBound(&strm, (uLong)len);
	if (strm.avail_out > INT_MAX) {
		deflateEnd(&strm);
		return SZL_ERR;
	}

	buf = (Bytef *)szl_malloc(interp, (size_t)strm.avail_out);
	if (!buf) {
		deflateEnd(&strm);
		return SZL_ERR;
	}

	strm.next_out = buf;
	strm.next_in = (Bytef *)in;
	strm.avail_in = (uInt)len;

	/* always compress in one pass - the return value holds the entire
	 * decompressed data anyway, so there's no reason to do chunked
	 * decompression */
	if (deflate(&strm, Z_FINISH) != Z_STREAM_END) {
		free(strm.next_out);
		deflateEnd(&strm);
		return SZL_ERR;
	}

	deflateEnd(&strm);

	if (strm.total_out > INT_MAX) {
		free(strm.next_out);
		return SZL_ERR;
	}

	obj = szl_new_str_noalloc(interp, (char *)buf, (size_t)strm.total_out);
	if (!obj) {
		free(buf);
		return SZL_ERR;
	}

	return szl_set_last(interp, obj);
}

static
enum szl_res szl_zlib_proc_deflate(struct szl_interp *interp,
                                   const unsigned int objc,
                                   struct szl_obj **objv)
{
	szl_int level = Z_DEFAULT_COMPRESSION;
	char *in;
	size_t len;

	if ((objc == 3) && !szl_as_int(interp, objv[2], &level))
		return SZL_ERR;

	if (!szl_as_str(interp, objv[1], &in, &len) || !len)
		return SZL_ERR;

	return szl_zlib_compress(interp, in, len, level, -MAX_WBITS);
}

static
enum szl_res szl_zlib_decompress(struct szl_interp *interp,
                                 const char *in,
                                 const size_t len,
                                 const szl_int bufsiz,
                                 const int wbits)
{
	z_stream strm = {0};
	void *buf;
	struct szl_obj *out;
	int res;

	if ((bufsiz <= 0) || (bufsiz > INT_MAX)) {
		szl_set_last_str(interp,
		                 "buffer size must be between 0 and "SZL_PASTE(INT_MAX),
		                 -1);
		return SZL_ERR;
	}

	if (inflateInit2(&strm, wbits) != Z_OK)
		return SZL_ERR;

	/* allocate a buffer - decompression is done in chunks, into this buffer;
	 * when the decompressed data size is given, decompression is faster because
	 * it's done in one pass, with less memcpy() overhead */
	buf = szl_malloc(interp, (size_t)bufsiz);
	if (!buf) {
		inflateEnd(&strm);
		return SZL_ERR;
	}

	out = szl_new_empty(interp);
	if (!out) {
		free(buf);
		inflateEnd(&strm);
		return SZL_ERR;
	}

	strm.next_in = (Bytef*)in;
	strm.avail_in = (uInt)len;
	do {
		do {
			strm.next_out = buf;
			strm.avail_out = (uInt)bufsiz;

			res = inflate(&strm, Z_NO_FLUSH);
			switch (res) {
			case Z_OK:
			case Z_STREAM_END:
				/* append each chunk to the output object */
				if (szl_str_append_str(interp,
				                       out,
				                       buf,
				                       (size_t)bufsiz - (size_t)strm.avail_out))
					break;

			default:
				szl_free(out);
				free(buf);
				inflateEnd(&strm);
				if (strm.msg != NULL)
					szl_set_last_str(interp, strm.msg, -1);
				return SZL_ERR;
			}
		} while (strm.avail_out == 0);
	} while (res != Z_STREAM_END);

	/* free memory used for decompression before we assign the return value */
	free(buf);
	inflateEnd(&strm);

	return szl_set_last(interp, out);
}

static
enum szl_res szl_zlib_proc_inflate(struct szl_interp *interp,
                                   const unsigned int objc,
                                   struct szl_obj **objv)
{
	char *in;
	szl_int bufsiz = DEF_DECOMPRESS_BUFSIZ;
	size_t len;

	if ((objc == 3) && (!szl_as_int(interp, objv[2], &bufsiz)))
		return SZL_ERR;

	if (!szl_as_str(interp, objv[1], &in, &len) || !len)
		return SZL_ERR;

	return szl_zlib_decompress(interp, in, len, bufsiz, -MAX_WBITS);
}

static
enum szl_res szl_zlib_proc_gzip(struct szl_interp *interp,
                                const unsigned int objc,
                                struct szl_obj **objv)
{
	szl_int level = Z_DEFAULT_COMPRESSION;
	char *in;
	size_t len;

	if ((objc == 3) && (!szl_as_int(interp, objv[2], &level)))
		return SZL_ERR;

	if (!szl_as_str(interp, objv[1], &in, &len) || !len)
		return SZL_ERR;

	return szl_zlib_compress(interp, in, len, level, WBITS_GZIP);
}

static
enum szl_res szl_zlib_proc_gunzip(struct szl_interp *interp,
                                  const unsigned int objc,
                                  struct szl_obj **objv)
{
	char *in;
	szl_int bufsiz = DEF_DECOMPRESS_BUFSIZ;
	size_t len;

	/* although the buffer can be up to LONG_MAX bytes, we want to limit it to
	 * INT_MAX because we create a string object later */
	if ((objc == 3) && !szl_as_int(interp, objv[2], &bufsiz))
		return SZL_ERR;

	if (!szl_as_str(interp, objv[1], &in, &len))
		return SZL_ERR;

	return szl_zlib_decompress(interp, in, len, bufsiz, WBITS_GZIP);
}

static
const struct szl_ext_export zlib_exports[] = {
	{
		SZL_PROC_INIT("zlib.crc32",
		              "str ?init?",
		              2,
		              3,
		              szl_zlib_proc_crc32,
		              NULL)
	},
	{
		SZL_PROC_INIT("zlib.deflate",
		              "str ?level?",
		              2,
		              3,
		              szl_zlib_proc_deflate,
		              NULL)
	},
	{
		SZL_PROC_INIT("zlib.inflate",
		              "str ?bufsiz?",
		              2,
		              3,
		              szl_zlib_proc_inflate,
		              NULL)
	},
	{
		SZL_PROC_INIT("zlib.gzip",
		              "str ?level?",
		              2,
		              3,
		              szl_zlib_proc_gzip,
		              NULL)
	},
	{
		SZL_PROC_INIT("zlib.gunzip",
		              "str ?bufsiz?",
		              2,
		              3,
		              szl_zlib_proc_gunzip,
		              NULL)
	}
};

int szl_init_zlib(struct szl_interp *interp)
{
	return szl_new_ext(interp,
	                   "zlib",
	                   zlib_exports,
	                   sizeof(zlib_exports) / sizeof(zlib_exports[0]));
}
