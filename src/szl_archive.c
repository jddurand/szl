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
#include <string.h>

#include <archive.h>
#include <archive_entry.h>

#include "szl.h"

struct szl_archive {
	struct archive *out;
	struct szl_obj *data;
};

static
enum szl_res szl_archive_iter(struct szl_interp *interp,
                              struct szl_archive *ar,
                              int (*cb)(struct szl_interp *,
                                        struct szl_archive *,
                                        struct archive *,
                                        struct archive_entry *,
                                        void *),
                              void *arg)
{
	struct archive *in;
	struct archive_entry *entry;
	const char *err;
	char *buf;
	size_t len;

	if (!szl_as_str(interp, ar->data, &buf, &len) || !len)
		return SZL_ERR;

	/* TODO: rewind the archive instead of re-reading it, if libarchive supports
	 * rewinding */
	in = archive_read_new();
	if (!in)
		return SZL_ERR;

	archive_read_support_filter_all(in);
	archive_read_support_format_all(in);

	if (archive_read_open_memory(in, (void *)buf, len) == 0) {
		do {
			switch (archive_read_next_header(in, &entry)) {
				case ARCHIVE_OK:
					break;

				case ARCHIVE_EOF:
					archive_read_free(in);
					return SZL_OK;

				default:
					goto bail;
			}

			if (!cb(interp, ar, in, entry, arg))
				break;
		} while (1);
	}

bail:
	err = archive_error_string(in);
	if (err)
		szl_set_last_str(interp, err, -1);
	archive_read_free(in);
	return SZL_ERR;
}

static
int szl_archive_append_path(struct szl_interp *interp,
                            struct szl_archive *ar,
                            struct archive *in,
                            struct archive_entry *entry,
                            void *arg)
{
	const char *path;

	path = archive_entry_pathname(entry);
	if (path)
		return szl_list_append_str(interp, (struct szl_obj *)arg, path, -1);

	return 0;
}

static
enum szl_res szl_archive_list(struct szl_interp *interp,
                              struct szl_archive *ar)
{
	struct szl_obj *paths;

	paths = szl_new_list(interp, NULL, 0);
	if (paths) {
		if (szl_archive_iter(interp,
		                     ar,
		                     szl_archive_append_path,
		                     paths) == SZL_OK)
			return szl_set_last(interp, paths);

		szl_free(paths);
	}

	return SZL_ERR;
}

static
int szl_archive_extract_file(struct szl_interp *interp,
                             struct szl_archive *ar,
                             struct archive *in,
                             struct archive_entry *entry,
                             void *arg)
{
	const void *blk;
	const char *err;
	size_t size;
	__LA_INT64_T off;

	if (archive_write_header(ar->out, entry) == ARCHIVE_OK) {
		do {
			switch (archive_read_data_block(in, &blk, &size, &off)) {
				case ARCHIVE_EOF:
					return 1;

				case ARCHIVE_OK:
					if (archive_write_data_block(ar->out,
					                             blk,
					                             size,
					                             off) == ARCHIVE_OK)
						break;

					err = archive_error_string(ar->out);
					goto bail;

				default:
					err = archive_error_string(in);
					goto bail;
			}
		} while (1);
	}
	else
		err = archive_error_string(ar->out);

bail:
	if (err)
		szl_set_last_str(interp, err, -1);
	return 0;
}

static
enum szl_res szl_archive_extract(struct szl_interp *interp,
                                 struct szl_archive *ar)
{
	return szl_archive_iter(interp, ar, szl_archive_extract_file, NULL);
}

static
enum szl_res szl_archive_proc(struct szl_interp *interp,
                              const unsigned int objc,
                              struct szl_obj **objv)
{
	char *op;
	size_t len;

	if (!szl_as_str(interp, objv[1], &op, &len) || !len)
		return SZL_ERR;

	if (strcmp("list", op) == 0)
		return szl_archive_list(interp, (struct szl_archive *)objv[0]->priv);
	else if (strcmp("extract", op) == 0) {
		return szl_archive_extract(interp, (struct szl_archive *)objv[0]->priv);
	}
	else
		return szl_set_last_help(interp, objv[0]);

	return SZL_OK;
}

static
void szl_archive_del(void *priv)
{
	struct szl_archive *ar = (struct szl_archive *)priv;

	szl_unref(ar->data);
	archive_write_close(ar->out);
	archive_write_free(ar->out);

	free(priv);
}

static
enum szl_res szl_archive_proc_open(struct szl_interp *interp,
                                   const unsigned int objc,
                                   struct szl_obj **objv)
{
	struct szl_obj *name, *proc;
	struct szl_archive *ar;

	ar = (struct szl_archive *)szl_malloc(interp, sizeof(*ar));
	if (!ar)
		return SZL_ERR;

	ar->out = archive_write_disk_new();
	if (!ar->out) {
		free(ar);
		return SZL_ERR;
	}

	archive_write_disk_set_options(ar->out,
	                               ARCHIVE_EXTRACT_OWNER |
	                               ARCHIVE_EXTRACT_PERM |
	                               ARCHIVE_EXTRACT_TIME |
	                               ARCHIVE_EXTRACT_UNLINK |
	                               ARCHIVE_EXTRACT_ACL |
	                               ARCHIVE_EXTRACT_FFLAGS |
	                               ARCHIVE_EXTRACT_XATTR);
	archive_write_disk_set_standard_lookup(ar->out);

	name = szl_new_str_fmt(interp, "archive:%"PRIxPTR, (uintptr_t)ar);
	if (!name) {
		archive_write_free(ar->out);
		free(ar);
		return SZL_ERR;
	}

	proc = szl_new_proc(interp,
	                    name,
	                    2,
	                    2,
	                    "list|extract",
	                    szl_archive_proc,
	                    szl_archive_del,
	                    ar);

	if (!proc) {
		szl_free(name);
		archive_write_free(ar->out);
		free(ar);
		return SZL_ERR;
	}

	szl_unref(name);
	ar->data = szl_ref(objv[1]);
	return szl_set_last(interp, proc);
}

static
const struct szl_ext_export archive_exports[] = {
	{
		SZL_PROC_INIT("archive.open",
		              "open data",
		              2,
		              3,
		              szl_archive_proc_open,
		              NULL)
	}
};

int szl_init_archive(struct szl_interp *interp)
{
	return szl_new_ext(interp,
	                   "archive",
	                   archive_exports,
	                   sizeof(archive_exports) / sizeof(archive_exports[0]));
}
