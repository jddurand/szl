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
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#include <curl/curl.h>

#include "szl.h"

#define CONNECT_TIMEOUT 30
#define TIMEOUT 180

static
enum szl_res szl_curl_proc_encode(struct szl_interp *interp,
                                  const unsigned int objc,
                                  struct szl_obj **objv)
{
	char *s, *out;
	size_t len;
	enum szl_res res;

	if (!szl_as_str(interp, objv[1], &s, &len) || (len >= INT_MAX))
		return SZL_ERR;

	out = curl_easy_escape((CURL *)objv[0]->priv, s, (int)len);
	if (!out)
		return SZL_ERR;

	res = szl_set_last_str(interp, out, -1);
	curl_free(out);
	return res;
}

static
void szl_curl_encode_del(void *priv)
{
	curl_easy_cleanup((CURL *)priv);
}

static
enum szl_res szl_curl_proc_get(struct szl_interp *interp,
                               const unsigned int objc,
                               struct szl_obj **objv)
{
	sigset_t set, oldset, pend;
	CURLM *cm;
	CURL **cs;
	FILE **fhs;
	char **urls, **paths;
	const struct CURLMsg *info;
	size_t len;
	int n, i, j, act, nfds, q, err;
	CURLMcode m;
	enum szl_res res = SZL_ERR;

	if (objc % 2 == 0)
		return szl_set_last_help(interp, objv[0]);

	n = (objc - 1) / 2;

	cs = (CURL **)szl_malloc(interp, sizeof(CURL *) * n);
	if (!cs)
		return SZL_ERR;

	fhs = (FILE **)szl_malloc(interp, sizeof(FILE *) * n);
	if (!fhs) {
		free(cs);
		return SZL_ERR;
	}

	urls = (char **)szl_malloc(interp, sizeof(char *) * n);
	if (!urls) {
		free(fhs);
		free(cs);
		return SZL_ERR;
	}

	paths = (char **)szl_malloc(interp, sizeof(char *) * n);
	if (!paths) {
		free(urls);
		free(fhs);
		free(cs);
		return SZL_ERR;
	}

	cm = curl_multi_init();
	if (!cm)
		goto free_arrs;

	i = 1;
	j = 0;
	while (i < objc) {
		if (!szl_as_str(interp, objv[i], &urls[j], &len) ||
		    !len ||
		    !szl_as_str(interp, objv[i + 1], &paths[j], &len) ||
		    !len)
			goto cleanup_cm;

		i += 2;
		++j;
	}

	if ((sigemptyset(&set) == -1) ||
	    (sigaddset(&set, SIGTERM) == -1) ||
	    (sigaddset(&set, SIGINT) == -1) ||
	    (sigprocmask(SIG_BLOCK, &set, &oldset) == -1))
		goto cleanup_cm;

	for (i = 0; i < n; ++i) {
		fhs[i] = fopen(paths[i], "w");
		if (!fhs[i]) {
			err = errno;

			for (j = 0; j < i; ++j) {
				fclose(fhs[j]);
				unlink(paths[j]);
			}

			szl_set_last_fmt(interp,
			                   "failed to open %s: %s",
			                   paths[i],
			                   szl_set_last_strerror(interp, err));
			goto restore_sigmask;
		}
	}

	for (i = 0; i < n; ++i) {
		cs[i] = curl_easy_init();
		if (!cs[i]) {
			for (j = 0; j < i; ++j)
				curl_easy_cleanup(cs[j]);

			goto close_fhs;
		}
	}

	for (i = 0; i < n; ++i) {
		if ((curl_easy_setopt(cs[i], CURLOPT_FAILONERROR, 1) != CURLE_OK) ||
		    (curl_easy_setopt(cs[i], CURLOPT_TCP_NODELAY, 1) != CURLE_OK) ||
		    (curl_easy_setopt(cs[i], CURLOPT_USE_SSL, CURLUSESSL_TRY) != CURLE_OK) ||
		    (curl_easy_setopt(cs[i], CURLOPT_WRITEFUNCTION, fwrite) != CURLE_OK) ||
		    (curl_easy_setopt(cs[i], CURLOPT_WRITEDATA, fhs[i]) != CURLE_OK) ||
		    (curl_easy_setopt(cs[i], CURLOPT_URL, urls[i]) != CURLE_OK) ||
		    (curl_easy_setopt(cs[i], CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT) != CURLE_OK) ||
		    (curl_easy_setopt(cs[i], CURLOPT_TIMEOUT, TIMEOUT)) != CURLE_OK)
			goto cleanup_cs;
	}

	for (i = 0; i < n; ++i) {
		if (curl_multi_add_handle(cm, cs[i]) != CURLM_OK) {
			for (j = 0; j < i; ++j)
				curl_multi_remove_handle(cm, cs[j]);

			goto cleanup_cs;
		}
	}

	do {
		if ((sigpending(&pend) == -1) ||
		    (sigismember(&pend, SIGTERM)) ||
		    (sigismember(&pend, SIGINT)))
			break;

		m = curl_multi_perform(cm, &act);
		if (m != CURLM_OK) {
			szl_set_last_str(interp, curl_multi_strerror(m), -1);
			break;
		}

		if (act == 0) {
			info = curl_multi_info_read(cm, &q);
			if (info && (info->msg == CURLMSG_DONE)) {
				if (info->data.result == CURLE_OK)
					res = SZL_OK;
				else
					szl_set_last_str(interp,
					                 curl_easy_strerror(info->data.result),
					                 -1);
			}
			break;
		}

		m = curl_multi_wait(cm, NULL, 0, 1000, &nfds);
		if (m != CURLM_OK) {
			szl_set_last_str(interp, curl_multi_strerror(m), -1);
			break;
		}

		if (!nfds)
			usleep(100000);
	} while (1);

	for (i = 0; i < n; ++i)
		curl_multi_remove_handle(cm, cs[i]);

cleanup_cs:
	for (i = 0; i < n; ++i)
		curl_easy_cleanup(cs[i]);

close_fhs:
	for (i = 0; i < n; ++i)
		fclose(fhs[i]);

	if (res != SZL_OK) {
		for (i = 0; i < n; ++i)
			unlink(paths[i]);
	}

restore_sigmask:
	sigprocmask(SIG_SETMASK, &oldset, NULL);

cleanup_cm:
	curl_multi_cleanup(cm);

free_arrs:
	free(paths);
	free(urls);
	free(fhs);
	free(cs);

	return res;
}

int szl_init_curl(struct szl_interp *interp)
{
	static struct szl_ext_export curl_exports[] = {
		{
			SZL_PROC_INIT("curl.encode",
			              "str",
			              2,
			              2,
			              szl_curl_proc_encode,
			              szl_curl_encode_del)
		},
		{
			SZL_PROC_INIT("curl.get",
			              "url path...",
			              3,
			              -1,
			              szl_curl_proc_get,
			              NULL)
		}
	};

	curl_exports[0].val.proc.priv = curl_easy_init();
	if (!curl_exports[0].val.proc.priv)
		return 0;

	if (!szl_new_ext(interp,
	                 "curl",
	                 curl_exports,
	                 sizeof(curl_exports) / sizeof(curl_exports[0]))) {
		curl_easy_cleanup((CURL *)curl_exports[0].val.proc.priv);
		return 0;
	}

	return 1;
}
