/*
 * this file is part of szl.
 *
 * Copyright (c) 2016 Dima Krasner
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

#include "szl.h"

static enum szl_res szl_obj_proc_global(struct szl_interp *interp,
                                        const int objc,
                                        struct szl_obj **objv,
                                        struct szl_obj **ret)
{
	enum szl_res res;
	const char *s;

	s = szl_obj_str(objv[1], NULL);
	if (!s)
		return SZL_ERR;

	res = szl_local(interp, interp->global, s, objv[2]);
	/* return the value upon success - useful for one-liners */
	if (res == SZL_OK) {
		szl_set_result(ret, szl_obj_ref(objv[2]));
		if (!*ret)
			return SZL_ERR;
	}

	return res;
}

static enum szl_res szl_obj_proc_local(struct szl_interp *interp,
                                       const int objc,
                                       struct szl_obj **objv,
                                       struct szl_obj **ret)
{
	enum szl_res res;
	const char *name;

	name = szl_obj_str(objv[1], NULL);
	if (!name)
		return SZL_ERR;

	szl_obj_ref(interp->caller);
	res = szl_local(interp, interp->caller, name, objv[2]);
	szl_obj_unref(interp->caller);

	/* see the comment in szl_obj_proc_set() */
	if (res == SZL_OK) {
		szl_set_result(ret, szl_obj_ref(objv[2]));
		if (!*ret)
			return SZL_ERR;
	}

	return res;
}

static enum szl_res szl_obj_proc_eval(struct szl_interp *interp,
                                      const int objc,
                                      struct szl_obj **objv,
                                      struct szl_obj **ret)
{
	const char *s;
	size_t len;

	s = szl_obj_str(objv[1], &len);
	if (!s || !len)
		return SZL_ERR;

	return szl_run_const(interp, ret, s, len);
}

enum szl_res szl_init_obj(struct szl_interp *interp)
{
	if ((!szl_new_proc(interp,
	                   "global",
	                   3,
	                   3,
	                   "global name val",
	                   szl_obj_proc_global,
	                   NULL,
	                   NULL)) ||
	    (!szl_new_proc(interp,
	                   "local",
	                   3,
	                   3,
	                   "local name val",
	                   szl_obj_proc_local,
	                   NULL,
	                   NULL)) ||
	    (!szl_new_proc(interp,
	                   "eval",
	                   2,
	                   2,
	                   "eval exp",
	                   szl_obj_proc_eval,
	                   NULL,
	                   NULL)))
		return SZL_ERR;

	return SZL_OK;
}
