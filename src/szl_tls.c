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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <openssl/ssl.h>
#include <openssl/engine.h>
#include <openssl/err.h>
#include <openssl/conf.h>

#include "szl.h"

static SSL_CTX *szl_tls_ctx = NULL;

static
ssize_t szl_tls_read(struct szl_interp *interp,
                     void *priv,
                     unsigned char *buf,
                     const size_t len,
                     int *more)
{
	int out, err;

	out = SSL_read((SSL *)priv, buf, (int)len);
	if (out > 0)
		return (ssize_t)out;

	err = SSL_get_error((SSL *)priv, out);

	if (SSL_get_shutdown((SSL *)priv) & SSL_RECEIVED_SHUTDOWN)
		*more = 0;

	if ((err == SSL_ERROR_WANT_READ) ||
	    (err == SSL_ERROR_WANT_WRITE) ||
	    (err == SSL_ERROR_ZERO_RETURN))
		return 0;

	szl_set_last_str(interp, ERR_error_string(ERR_get_error(), NULL), -1);
	return -1;
}

static
ssize_t szl_tls_write(struct szl_interp *interp,
                      void *priv,
                      const unsigned char *buf,
                      const size_t len)
{
	ssize_t out;

	out = (ssize_t)SSL_write((SSL *)priv, buf, (int)len);
	if (out < 0) {
		szl_set_last_str(interp, ERR_error_string(ERR_get_error(), NULL), -1);
		return -1;
	}

	return out;
}

static
enum szl_res szl_tls_unblock(struct szl_interp *interp, void *priv)
{
	int fl, fd;
	BIO *rbio, *wbio;

	fd = SSL_get_fd((SSL *)priv);
	if (fd < 0)
		return SZL_ERR;

	fl = fcntl(fd, F_GETFL);
	if ((fl < 0) || (fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0))
		return szl_set_last_strerror(interp, errno);

	rbio = SSL_get_rbio((SSL *)priv);
	if (!rbio)
		return SZL_ERR;

	BIO_set_nbio(rbio, 1);

	wbio = SSL_get_wbio((SSL *)priv);
	if (!wbio)
		return SZL_ERR;

	if (wbio != rbio)
		BIO_set_nbio(wbio, 1);

	return SZL_OK;
}

static
void szl_tls_close(void *priv)
{
	int fd;

	fd = SSL_get_fd((SSL *)priv);

	SSL_free((SSL *)priv);
	if (fd >= 0)
		close(fd);
}

static
szl_int szl_tls_handle(void *priv)
{
	return (szl_int)SSL_get_fd((SSL *)priv);
}

static
const struct szl_stream_ops szl_tls_ops = {
	.read = szl_tls_read,
	.write = szl_tls_write,
	.close = szl_tls_close,
	.handle = szl_tls_handle,
	.unblock = szl_tls_unblock
};

static
enum szl_res szl_tls_new(struct szl_interp *interp,
                         int fd,
                         const int server,
                         const char *cert,
                         const char *priv)
{
	struct szl_obj *obj;
	struct szl_stream *strm;
	SSL *ssl;
	int err;

	if (!szl_tls_ctx)
		return SZL_ERR;

	strm = (struct szl_stream *)szl_malloc(interp, sizeof(struct szl_stream));
	if (!strm)
		return SZL_ERR;

	ssl = SSL_new(szl_tls_ctx);
	if (!ssl) {
		free(strm);
		return SZL_ERR;
	}

	fd = dup(fd);
	if (fd < 0) {
		err = errno;
		free(strm);
		return szl_set_last_strerror(interp, err);
	}

	if (SSL_set_fd(ssl, fd) == 0) {
		close(fd);
		SSL_free(ssl);
		free(strm);
		return SZL_ERR;
	}

	SSL_set_cipher_list(ssl, "ALL");

	if (server) {
		if ((SSL_use_certificate_file(ssl, cert, SSL_FILETYPE_PEM) != 1) ||
		    (SSL_use_PrivateKey_file(ssl, priv, SSL_FILETYPE_PEM) != 1)) {
			close(fd);
			SSL_free(ssl);
			free(strm);
			szl_set_last_str(interp,
			                 ERR_error_string(ERR_get_error(), NULL),
			                 -1);
			return SZL_ERR;
		}

		SSL_set_accept_state(ssl);
	}
	else
		SSL_set_connect_state(ssl);

	strm->priv = ssl;
	strm->ops = &szl_tls_ops;
	strm->flags = SZL_STREAM_BLOCKING;
	strm->buf = NULL;

	obj = szl_new_stream(interp, strm, server ? "tls.server" : "tls.client");
	if (!obj) {
		szl_stream_free(strm);
		return SZL_ERR;
	}

	return szl_set_last(interp, obj);
}

static
enum szl_res szl_tls_proc_connect(struct szl_interp *interp,
                                  const unsigned int objc,
                                  struct szl_obj **objv)
{
	szl_int fd;

	if ((!szl_as_int(interp, objv[1], &fd)) || (fd < 0) || (fd > INT_MAX))
		return SZL_ERR;

	return szl_tls_new(interp, (int)fd, 0, NULL, NULL);
}

static
enum szl_res szl_tls_proc_accept(struct szl_interp *interp,
                                 const unsigned int objc,
                                 struct szl_obj **objv)
{
	char *cert, *priv;
	szl_int fd;
	size_t len;

	if ((!szl_as_int(interp, objv[1], &fd)) || (fd < 0) || (fd > INT_MAX))
		return SZL_ERR;

	if (!szl_as_str(interp, objv[2], &cert, &len) ||
	    !len ||
	    !szl_as_str(interp, objv[3], &priv, &len) ||
	    !len)
		return SZL_ERR;

	return szl_tls_new(interp, (int)fd, 1, cert, priv);
}

__attribute__((__destructor__))
static void szl_del_tls(void)
{
	if (szl_tls_ctx)
		SSL_CTX_free(szl_tls_ctx);

	FIPS_mode_set(0);
	ENGINE_cleanup();
	CONF_modules_unload(1);
	EVP_cleanup();
	CRYPTO_cleanup_all_ex_data();
	ERR_remove_state(0);
	ERR_free_strings();
}

static
const struct szl_ext_export tls_exports[] = {
	{
		SZL_PROC_INIT("tls.connect",
		              "handle",
		              2,
		              2,
		              szl_tls_proc_connect,
		              NULL)
	},
	{
		SZL_PROC_INIT("tls.accept",
		              "handle cert priv",
		              4,
		              4,
		              szl_tls_proc_accept,
		              NULL)
	}
};

int szl_init_tls(struct szl_interp *interp)
{
	SSL_load_error_strings();
	SSL_library_init();

	szl_tls_ctx = SSL_CTX_new(TLSv1_2_method());
	if (!szl_tls_ctx)
		return 0;

	if (SSL_CTX_set_default_verify_paths(szl_tls_ctx)) {
		SSL_CTX_set_verify(szl_tls_ctx, SSL_VERIFY_NONE, NULL);

		if (szl_new_ext(interp,
	                    "tls",
		                tls_exports,
		                sizeof(tls_exports) / sizeof(tls_exports[0])))
			return 1;
	}

	SSL_CTX_free(szl_tls_ctx);
	ERR_free_strings();
	return 0;
}
