# nginx-ip-anon-module

[![Build & Test][build-test-badge]][build-test-link]
[![Addresses][addresses-badge]][addresses-link]
[![FreeBSD][freebsd-badge]][freebsd-link]
[![A/UBSan][sanitizers-badge]][sanitizers-link]
[![Valgrind][valgrind-badge]][valgrind-link]
[![Reload][reload-badge]][reload-link]
[![CodeQL][codeql-badge]][codeql-link]
[![Lint][lint-badge]][lint-link]

[build-test-badge]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/build-test.yml/badge.svg
[build-test-link]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/build-test.yml
[addresses-badge]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/addresses.yml/badge.svg
[addresses-link]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/addresses.yml
[freebsd-badge]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/freebsd.yml/badge.svg
[freebsd-link]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/freebsd.yml
[sanitizers-badge]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/sanitizers.yml/badge.svg
[sanitizers-link]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/sanitizers.yml
[valgrind-badge]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/valgrind.yml/badge.svg
[valgrind-link]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/valgrind.yml
[reload-badge]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/reload.yml/badge.svg
[reload-link]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/reload.yml
[codeql-badge]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/codeql.yml/badge.svg
[codeql-link]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/codeql.yml
[lint-badge]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/lint.yml/badge.svg
[lint-link]: https://github.com/sysadmin-labs/nginx-ip-anon-module/actions/workflows/lint.yml

Two variables that carry the client address with its host part cut off, so
a log can be kept without keeping the address that identifies the client.

nginx has nothing for this.  `$remote_addr` is the whole address, and the
usual workarounds -- a `map` with a regular expression, or a log format that
drops the address altogether -- either break on IPv6 or throw away more than
the question asks for.

## Synopsis

```nginx
http {
    ip_anon_v4_prefix 24;        # the default: the last octet goes
    ip_anon_v6_prefix 48;        # the default: the routing prefix stays

    log_format anonymised '$remote_addr_anon - - [$time_local] "$request" '
                          '$status $body_bytes_sent';

    server {
        access_log /var/log/nginx/access.log anonymised;

        location /strict {
            ip_anon_v4_prefix 16;
            ip_anon_v6_prefix 32;
        }
    }
}
```

## Variables

### `$remote_addr_anon`

The client address with everything below the prefix set to zero, written
the way nginx writes `$remote_addr`.  On a unix socket the variable is not
found, which is what `$remote_addr` does there as well.

### `$binary_remote_addr_anon`

The same address in its binary form, four bytes for IPv4 and sixteen for
IPv6 -- the shape `limit_req_zone` and `limit_conn_zone` want for a key.
Rate limiting on an anonymised address still groups the clients of one
network together, which is usually what such a limit is for.

## Directives

### `ip_anon_v4_prefix`

**syntax:** `ip_anon_v4_prefix <0-32>`
**default:** `24`
**context:** `http`, `server`, `location`

How many bits of an IPv4 address are kept.  `24` keeps the network and
drops the host, `0` drops the address entirely, `32` keeps it whole.

### `ip_anon_v6_prefix`

**syntax:** `ip_anon_v6_prefix <0-128>`
**default:** `48`
**context:** `http`, `server`, `location`

How many bits of an IPv6 address are kept.  An address that carries an IPv4
address inside it -- `::ffff:192.0.2.1`, which is what a dual stack listener
hands to the module -- is masked with the **IPv4** prefix instead, because
the v6 prefix would cut into the `::ffff:` part and leave the four bytes
that name the client untouched.

A value wider than the address family is refused at configuration time
rather than quietly clamped.

## Building

```sh
./configure --add-module=/path/to/nginx-ip-anon-module
```

or, as a loadable module,

```sh
./configure --add-dynamic-module=/path/to/nginx-ip-anon-module
```

and then `load_module modules/ngx_http_ip_anon_module.so;` in the main
context.

## Testing

```sh
ci/build.sh 1.30.5 /tmp/nginx-test
TEST_NGINX_BINARY=/tmp/nginx-test/sbin/nginx prove -r t/
ci/addresses.sh /tmp/nginx-test/sbin/nginx
ci/reload.sh /tmp/nginx-test/sbin/nginx
```

## License

BSD 2-Clause.  See [LICENSE](LICENSE).
