/*
 * Copyright (c) 2026, Jochen Neumeister <joneum@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DAMAGES WHATSOEVER
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_md5.h>


#define NGX_HTTP_IP_ANON_MD5_LEN   16
#define NGX_HTTP_IP_ANON_HMAC_BLK  64


typedef struct {
    ngx_uint_t  v4_prefix;    /* bits kept of an IPv4 address */
    ngx_uint_t  v6_prefix;    /* bits kept of an IPv6 address */
    ngx_str_t   hash_secret;  /* HMAC key; empty disables the pseudonym */
    time_t      hash_period;  /* rotation window in seconds; 0 is no rotation */
    ngx_uint_t  hash_length;  /* hex characters of pseudonym to emit */
} ngx_http_ip_anon_conf_t;


static ngx_int_t ngx_http_ip_anon_add_variables(ngx_conf_t *cf);
static void *ngx_http_ip_anon_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_ip_anon_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);
static ngx_int_t ngx_http_ip_anon_text_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_ip_anon_binary_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_ip_anon_hash_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);


/*
 * A prefix of 0 is valid and drops the address.  A prefix wider than the
 * address family is rejected, not clamped.
 */
static ngx_conf_num_bounds_t  ngx_http_ip_anon_v4_bounds = {
    ngx_conf_check_num_bounds, 0, 32
};

static ngx_conf_num_bounds_t  ngx_http_ip_anon_v6_bounds = {
    ngx_conf_check_num_bounds, 0, 128
};

/* a prefix of the MD5 digest; 32 hex characters is the whole of it */
static ngx_conf_num_bounds_t  ngx_http_ip_anon_hash_len_bounds = {
    ngx_conf_check_num_bounds, 1, 32
};


static ngx_command_t  ngx_http_ip_anon_commands[] = {

    { ngx_string("ip_anon_v4_prefix"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ip_anon_conf_t, v4_prefix),
      &ngx_http_ip_anon_v4_bounds },

    { ngx_string("ip_anon_v6_prefix"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ip_anon_conf_t, v6_prefix),
      &ngx_http_ip_anon_v6_bounds },

    { ngx_string("ip_anon_hash_secret"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ip_anon_conf_t, hash_secret),
      NULL },

    { ngx_string("ip_anon_hash_period"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_sec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ip_anon_conf_t, hash_period),
      NULL },

    { ngx_string("ip_anon_hash_length"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ip_anon_conf_t, hash_length),
      &ngx_http_ip_anon_hash_len_bounds },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_ip_anon_module_ctx = {
    ngx_http_ip_anon_add_variables,        /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_ip_anon_create_loc_conf,      /* create location configuration */
    ngx_http_ip_anon_merge_loc_conf        /* merge location configuration */
};


ngx_module_t  ngx_http_ip_anon_module = {
    NGX_MODULE_V1,
    &ngx_http_ip_anon_module_ctx,          /* module context */
    ngx_http_ip_anon_commands,             /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_variable_t  ngx_http_ip_anon_variables[] = {

    { ngx_string("remote_addr_anon"), NULL,
      ngx_http_ip_anon_text_variable, 0, NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("binary_remote_addr_anon"), NULL,
      ngx_http_ip_anon_binary_variable, 0, NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("remote_addr_hash"), NULL,
      ngx_http_ip_anon_hash_variable, 0, NGX_HTTP_VAR_NOCACHEABLE, 0 },

      ngx_http_null_variable
};


/*
 * Clear all bits below the prefix: whole bytes first, then the byte the
 * prefix ends in.  A prefix that is a multiple of eight never reaches the
 * shift, which is deliberate: shifting a u_char by eight is undefined.
 */
static void
ngx_http_ip_anon_mask(u_char *addr, size_t len, ngx_uint_t prefix)
{
    size_t  i;

    for (i = 0; i < len; i++) {

        if (prefix >= 8) {
            prefix -= 8;
            continue;
        }

        addr[i] = (u_char) (prefix ? (addr[i] & (0xff << (8 - prefix))) : 0);
        prefix = 0;
    }
}


/*
 * Mask a copy of the client address.  r->connection->sockaddr is the live
 * address of the connection and nginx reads it again for its own variables
 * and for the error log, so it has to stay untouched.  The copy goes into
 * ngx_sockaddr_t, the union nginx uses for this, and not into a byte array:
 * a sockaddr_in6 read out of a buffer with one byte alignment is undefined.
 */
static ngx_int_t
ngx_http_ip_anon_sockaddr(ngx_http_request_t *r, ngx_sockaddr_t *sa)
{
    ngx_http_ip_anon_conf_t  *acf;
    struct sockaddr_in       *sin;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6      *sin6;
#endif

    if (r->connection->socklen == 0
        || r->connection->socklen > sizeof(ngx_sockaddr_t))
    {
        return NGX_DECLINED;
    }

    acf = ngx_http_get_module_loc_conf(r, ngx_http_ip_anon_module);

    /*
     * The copy is as long as the address, the union is longer: zero it, or
     * what is masked and formatted beyond socklen is whatever was on the
     * stack.
     */
    ngx_memzero(sa, sizeof(ngx_sockaddr_t));
    ngx_memcpy(sa, r->connection->sockaddr, r->connection->socklen);

    switch (sa->sockaddr.sa_family) {

    case AF_INET:
        sin = &sa->sockaddr_in;
        ngx_http_ip_anon_mask((u_char *) &sin->sin_addr, 4, acf->v4_prefix);
        return NGX_OK;

#if (NGX_HAVE_INET6)
    case AF_INET6:
        sin6 = &sa->sockaddr_in6;

        /*
         * An IPv4-mapped address is masked with the v4 prefix.  The v6
         * prefix would cut into the ::ffff: part and leave the four bytes
         * identifying the client untouched.
         */
        if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
            ngx_http_ip_anon_mask(sin6->sin6_addr.s6_addr + 12, 4,
                                  acf->v4_prefix);

        } else {
            ngx_http_ip_anon_mask(sin6->sin6_addr.s6_addr, 16,
                                  acf->v6_prefix);
        }

        return NGX_OK;
#endif

    default:
        /* unix sockets and anything else: there is no host part to mask */
        return NGX_DECLINED;
    }
}


/*
 * A family without an address to mask falls back to connection->addr_text,
 * which is what $remote_addr and $binary_remote_addr hand out there ("unix:"
 * on a unix socket).  A variable without a value is an empty key, and
 * limit_req and limit_conn skip an empty key: the limit would quietly stop
 * applying to those connections.
 */
static ngx_int_t
ngx_http_ip_anon_addr_text(ngx_http_request_t *r,
    ngx_http_variable_value_t *v)
{
    v->len = r->connection->addr_text.len;
    v->data = r->connection->addr_text.data;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_http_ip_anon_text_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char         *text;
    size_t          len;
    ngx_sockaddr_t  sa;

    if (ngx_http_ip_anon_sockaddr(r, &sa) != NGX_OK) {
        return ngx_http_ip_anon_addr_text(r, v);
    }

    text = ngx_pnalloc(r->pool, NGX_SOCKADDR_STRLEN);
    if (text == NULL) {
        return NGX_ERROR;
    }

    /*
     * ngx_sock_ntop() is what $remote_addr itself uses, so the masked value
     * is written exactly as the unmasked one would be.
     */
    len = ngx_sock_ntop(&sa.sockaddr, r->connection->socklen,
                        text, NGX_SOCKADDR_STRLEN, 0);

    v->len = len;
    v->data = text;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_http_ip_anon_binary_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char               *addr;
    size_t                len;
    ngx_sockaddr_t        sa;
    struct sockaddr_in   *sin;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6  *sin6;
#endif

    if (ngx_http_ip_anon_sockaddr(r, &sa) != NGX_OK) {
        return ngx_http_ip_anon_addr_text(r, v);
    }

    switch (sa.sockaddr.sa_family) {

    case AF_INET:
        sin = &sa.sockaddr_in;
        addr = (u_char *) &sin->sin_addr;
        len = 4;
        break;

#if (NGX_HAVE_INET6)
    case AF_INET6:
        sin6 = &sa.sockaddr_in6;
        addr = sin6->sin6_addr.s6_addr;
        len = 16;
        break;
#endif

    default:
        return ngx_http_ip_anon_addr_text(r, v);
    }

    v->data = ngx_pnalloc(r->pool, len);
    if (v->data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(v->data, addr, len);

    v->len = len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    return NGX_OK;
}


/*
 * HMAC-MD5 (RFC 2104) over nginx' built-in MD5.  MD5 is the one keyed hash
 * that nginx always provides without OpenSSL, so the pseudonym works in a
 * build without SSL too; as a keyed hash its collision weakness does not
 * apply here.  A key longer than the block is replaced by its own digest.
 */
static void
ngx_http_ip_anon_hmac_md5(const u_char *key, size_t keylen,
    const u_char *msg, size_t msglen, u_char digest[NGX_HTTP_IP_ANON_MD5_LEN])
{
    ngx_md5_t  ctx;
    u_char     k[NGX_HTTP_IP_ANON_HMAC_BLK];
    u_char     ipad[NGX_HTTP_IP_ANON_HMAC_BLK];
    u_char     opad[NGX_HTTP_IP_ANON_HMAC_BLK];
    u_char     inner[NGX_HTTP_IP_ANON_MD5_LEN];
    size_t     i;

    ngx_memzero(k, sizeof(k));

    if (keylen > NGX_HTTP_IP_ANON_HMAC_BLK) {
        ngx_md5_init(&ctx);
        ngx_md5_update(&ctx, key, keylen);
        ngx_md5_final(k, &ctx);

    } else {
        ngx_memcpy(k, key, keylen);
    }

    for (i = 0; i < NGX_HTTP_IP_ANON_HMAC_BLK; i++) {
        ipad[i] = (u_char) (k[i] ^ 0x36);
        opad[i] = (u_char) (k[i] ^ 0x5c);
    }

    ngx_md5_init(&ctx);
    ngx_md5_update(&ctx, ipad, NGX_HTTP_IP_ANON_HMAC_BLK);
    ngx_md5_update(&ctx, msg, msglen);
    ngx_md5_final(inner, &ctx);

    ngx_md5_init(&ctx);
    ngx_md5_update(&ctx, opad, NGX_HTTP_IP_ANON_HMAC_BLK);
    ngx_md5_update(&ctx, inner, NGX_HTTP_IP_ANON_MD5_LEN);
    ngx_md5_final(digest, &ctx);
}


/*
 * A one-way pseudonym of the client address: HMAC-MD5 of the binary address
 * under the configured secret, emitted as hex.  Without a secret the variable
 * is absent (opt-in).  With ip_anon_hash_period the current window number is
 * mixed in, so the pseudonym is stable inside a window and unlinkable across
 * windows.  The full address is hashed, not the masked prefix, so the two
 * modes are independent.
 */
static ngx_int_t
ngx_http_ip_anon_hash_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_http_ip_anon_conf_t  *acf;
    u_char                   *addr, *hex;
    size_t                    len, nbytes, msglen;
    u_char                    msg[16 + sizeof(uint64_t)];
    u_char                    digest[NGX_HTTP_IP_ANON_MD5_LEN];
    struct sockaddr_in       *sin;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6      *sin6;
#endif

    acf = ngx_http_get_module_loc_conf(r, ngx_http_ip_anon_module);

    if (acf->hash_secret.len == 0 || r->connection->socklen == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    switch (r->connection->sockaddr->sa_family) {

    case AF_INET:
        sin = (struct sockaddr_in *) r->connection->sockaddr;
        addr = (u_char *) &sin->sin_addr;
        len = 4;
        break;

#if (NGX_HAVE_INET6)
    case AF_INET6:
        sin6 = (struct sockaddr_in6 *) r->connection->sockaddr;

        /* v4-mapped: hash the four v4 bytes, as the mask path treats it */
        if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
            addr = sin6->sin6_addr.s6_addr + 12;
            len = 4;

        } else {
            addr = sin6->sin6_addr.s6_addr;
            len = 16;
        }
        break;
#endif

    default:
        /* unix sockets and anything without an address: no pseudonym */
        v->not_found = 1;
        return NGX_OK;
    }

    ngx_memcpy(msg, addr, len);
    msglen = len;

    if (acf->hash_period > 0) {
        uint64_t    bucket = (uint64_t) ngx_time() / (uint64_t) acf->hash_period;
        ngx_uint_t  i;

        /* big-endian, so the pseudonym does not depend on the host byte order */
        for (i = 0; i < sizeof(uint64_t); i++) {
            msg[msglen + sizeof(uint64_t) - 1 - i] = (u_char) (bucket & 0xff);
            bucket >>= 8;
        }

        msglen += sizeof(uint64_t);
    }

    ngx_http_ip_anon_hmac_md5(acf->hash_secret.data, acf->hash_secret.len,
                              msg, msglen, digest);

    nbytes = (acf->hash_length + 1) / 2;   /* digest bytes rendered as hex */

    hex = ngx_pnalloc(r->pool, nbytes * 2);
    if (hex == NULL) {
        return NGX_ERROR;
    }

    ngx_hex_dump(hex, digest, nbytes);

    v->len = acf->hash_length;             /* an odd length drops the last nibble */
    v->data = hex;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_http_ip_anon_add_variables(ngx_conf_t *cf)
{
    ngx_http_variable_t  *var, *v;

    for (v = ngx_http_ip_anon_variables; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}


static void *
ngx_http_ip_anon_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_ip_anon_conf_t  *acf;

    acf = ngx_pcalloc(cf->pool, sizeof(ngx_http_ip_anon_conf_t));
    if (acf == NULL) {
        return NULL;
    }

    acf->v4_prefix = NGX_CONF_UNSET_UINT;
    acf->v6_prefix = NGX_CONF_UNSET_UINT;
    acf->hash_period = NGX_CONF_UNSET;
    acf->hash_length = NGX_CONF_UNSET_UINT;
    /* hash_secret stays {0, NULL} from pcalloc: the pseudonym is off */

    return acf;
}


static char *
ngx_http_ip_anon_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_ip_anon_conf_t  *prev = parent;
    ngx_http_ip_anon_conf_t  *conf = child;

    /* the widths data protection authorities and analytics tools use */
    ngx_conf_merge_uint_value(conf->v4_prefix, prev->v4_prefix, 24);
    ngx_conf_merge_uint_value(conf->v6_prefix, prev->v6_prefix, 48);

    ngx_conf_merge_str_value(conf->hash_secret, prev->hash_secret, "");
    ngx_conf_merge_sec_value(conf->hash_period, prev->hash_period, 0);
    ngx_conf_merge_uint_value(conf->hash_length, prev->hash_length, 16);

    return NGX_CONF_OK;
}
