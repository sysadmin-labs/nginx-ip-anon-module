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


typedef struct {
    ngx_uint_t  v4_prefix;   /* bits kept of an IPv4 address */
    ngx_uint_t  v6_prefix;   /* bits kept of an IPv6 address */
} ngx_http_ip_anon_conf_t;


static ngx_int_t ngx_http_ip_anon_add_variables(ngx_conf_t *cf);
static void *ngx_http_ip_anon_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_ip_anon_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);
static ngx_int_t ngx_http_ip_anon_text_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_ip_anon_binary_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);


/*
 * A prefix of zero is allowed -- it drops the address entirely, which is a
 * legitimate setting -- but a value wider than the address family is
 * refused rather than silently clamped: a configuration that says 64 for
 * IPv4 means something its author did not check.
 */
static ngx_conf_num_bounds_t  ngx_http_ip_anon_v4_bounds = {
    ngx_conf_check_num_bounds, 0, 32
};

static ngx_conf_num_bounds_t  ngx_http_ip_anon_v6_bounds = {
    ngx_conf_check_num_bounds, 0, 128
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

      ngx_http_null_variable
};


/*
 * Zero every bit below the prefix.  The loop walks whole bytes first and
 * then the one byte the prefix ends inside; a prefix that is a multiple of
 * eight never reaches the shift, which matters because a shift by eight of
 * an eight bit value is undefined, not zero.
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
 * Copy the client address, mask it, and hand back the copy.  Working on a
 * copy is the whole point: r->connection->sockaddr is the live address of
 * the connection, and nginx reads it again for its own variables and for
 * the error log.
 */
static ngx_int_t
ngx_http_ip_anon_sockaddr(ngx_http_request_t *r, u_char *buf, size_t size)
{
    ngx_http_ip_anon_conf_t  *acf;
    struct sockaddr_in       *sin;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6      *sin6;
#endif

    if (r->connection->socklen == 0 || r->connection->socklen > size) {
        return NGX_DECLINED;
    }

    acf = ngx_http_get_module_loc_conf(r, ngx_http_ip_anon_module);

    ngx_memcpy(buf, r->connection->sockaddr, r->connection->socklen);

    switch (((struct sockaddr *) buf)->sa_family) {

    case AF_INET:
        sin = (struct sockaddr_in *) buf;
        ngx_http_ip_anon_mask((u_char *) &sin->sin_addr, 4, acf->v4_prefix);
        return NGX_OK;

#if (NGX_HAVE_INET6)
    case AF_INET6:
        sin6 = (struct sockaddr_in6 *) buf;

        /*
         * An address that carries an IPv4 one inside it is masked as the
         * IPv4 address it is -- the v6 prefix would otherwise cut into the
         * ::ffff: part and leave the four bytes that actually identify the
         * client untouched.
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
        /* unix sockets and anything else: nothing to anonymise */
        return NGX_DECLINED;
    }
}


static ngx_int_t
ngx_http_ip_anon_text_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char      sa[NGX_SOCKADDRLEN];
    u_char     *text;
    size_t      len;

    if (ngx_http_ip_anon_sockaddr(r, sa, sizeof(sa)) != NGX_OK) {
        v->not_found = 1;
        return NGX_OK;
    }

    text = ngx_pnalloc(r->pool, NGX_SOCKADDR_STRLEN);
    if (text == NULL) {
        return NGX_ERROR;
    }

    /*
     * ngx_sock_ntop() is what nginx uses for $remote_addr itself, so the
     * anonymised value is written the same way the unmasked one would be,
     * brackets, scope id and all.
     */
    len = ngx_sock_ntop((struct sockaddr *) sa, r->connection->socklen,
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
    u_char                sa[NGX_SOCKADDRLEN];
    u_char               *addr;
    size_t                len;
    struct sockaddr_in   *sin;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6  *sin6;
#endif

    if (ngx_http_ip_anon_sockaddr(r, sa, sizeof(sa)) != NGX_OK) {
        v->not_found = 1;
        return NGX_OK;
    }

    switch (((struct sockaddr *) sa)->sa_family) {

    case AF_INET:
        sin = (struct sockaddr_in *) sa;
        addr = (u_char *) &sin->sin_addr;
        len = 4;
        break;

#if (NGX_HAVE_INET6)
    case AF_INET6:
        sin6 = (struct sockaddr_in6 *) sa;
        addr = sin6->sin6_addr.s6_addr;
        len = 16;
        break;
#endif

    default:
        v->not_found = 1;
        return NGX_OK;
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

    return acf;
}


static char *
ngx_http_ip_anon_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_ip_anon_conf_t  *prev = parent;
    ngx_http_ip_anon_conf_t  *conf = child;

    /*
     * 24 and 48 are what the German data protection authorities ask for
     * when they ask for shortened addresses, and what the common analytics
     * tools do: the last octet of an IPv4 address goes, and an IPv6
     * address is cut back to the routing prefix.
     */
    ngx_conf_merge_uint_value(conf->v4_prefix, prev->v4_prefix, 24);
    ngx_conf_merge_uint_value(conf->v6_prefix, prev->v6_prefix, 48);

    return NGX_CONF_OK;
}
