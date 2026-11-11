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

Masks the host part of the client address and provides it as
`$remote_addr_anon` and `$binary_remote_addr_anon`, for access logs and
limit keys that should not carry the address identifying the client.

What is masked is the address of the connection, so behind a proxy it is the
proxy unless `ngx_http_realip_module` has replaced it; with `set_real_ip_from`
and `real_ip_header` in place it is the client.

As an alternative to masking it can emit a one-way pseudonym of the address,
`$remote_addr_hash`: two clients in the same network stay apart, so a log can
count unique visitors and `limit_req` can act per client, yet the token does
not lead back to the address a masked prefix would still reveal a part of.

## Synopsis

```nginx
http {
    ip_anon_v4_prefix 24;
    ip_anon_v6_prefix 48;

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

The client address with all bits below the prefix cleared, formatted the way
nginx formats `$remote_addr`.  On a connection that carries no IP address, a
unix socket for instance, it reports what `$remote_addr` reports there,
`unix:`.

### `$binary_remote_addr_anon`

The same address in binary form, four bytes for IPv4 and sixteen for IPv6,
which is the form `limit_req_zone` and `limit_conn_zone` expect for a key.
A limit on the masked address groups a network's clients together.  Here too
a connection without an IP address falls back to what
`$binary_remote_addr` reports: a key that came out empty would silently
take those connections out of every limit.

### `$remote_addr_hash`

A one-way pseudonym of the client address: HMAC-MD5 of the address under a
secret, as hex.  The same client yields the same token, a different client a
different one, and the token does not lead back to the address.  Keyed, the
value is a pseudonym and not a checksum; HMAC-MD5 is the one keyed hash nginx
provides without OpenSSL, so the variable works in a build without SSL too.

Without `ip_anon_hash_secret` the variable is absent.  An IPv4-mapped address
is hashed over its four IPv4 bytes, as the masked form treats it; a connection
without an IP address, a unix socket for instance, has no token.

```nginx
http {
    ip_anon_hash_secret "change me";
    ip_anon_hash_period 24h;

    log_format pseudonymous '$remote_addr_hash - - [$time_local] "$request" '
                            '$status $body_bytes_sent';

    limit_req_zone $remote_addr_hash zone=perclient:10m rate=10r/s;
}
```

## Directives

### `ip_anon_v4_prefix`

**syntax:** `ip_anon_v4_prefix <0-32>`
**default:** `24`
**context:** `http`, `server`, `location`

Number of IPv4 address bits kept.  24 keeps the network and drops the host,
0 drops the address, 32 keeps it unchanged.

### `ip_anon_v6_prefix`

**syntax:** `ip_anon_v6_prefix <0-128>`
**default:** `48`
**context:** `http`, `server`, `location`

Number of IPv6 address bits kept.  An IPv4-mapped address such as
`::ffff:192.0.2.1`, which is what a dual stack listener passes to the
module, is masked with the IPv4 prefix instead: the IPv6 prefix would cut
into the `::ffff:` part and leave the four bytes identifying the client
untouched.

A prefix wider than its address family is rejected at configuration time,
not clamped.

### `ip_anon_hash_secret`

**syntax:** `ip_anon_hash_secret <string>`
**default:** none
**context:** `http`, `server`, `location`

The key `$remote_addr_hash` is derived with.  Without it the variable is
absent, so the pseudonym is opt-in.  Keep the secret to keep the tokens stable
across reloads and hosts; change it to make every token change.

### `ip_anon_hash_period`

**syntax:** `ip_anon_hash_period <time>`
**default:** `0`, no rotation
**context:** `http`, `server`, `location`

When set, the current window number (`now / period`) is mixed into the hash,
so a client's token is stable within a window and does not carry across
windows.  `24h` counts a visitor through a day but not from one day to the
next.

### `ip_anon_hash_length`

**syntax:** `ip_anon_hash_length <1-32>`
**default:** `16`
**context:** `http`, `server`, `location`

Number of hex characters emitted, up to the 32 of the full digest.  Fewer
characters make a shorter log at a higher chance of two addresses sharing a
token.

## Building

```sh
./configure --add-module=/path/to/nginx-ip-anon-module
```

or as a loadable module

```sh
./configure --add-dynamic-module=/path/to/nginx-ip-anon-module
```

with `load_module modules/ngx_http_ip_anon_module.so;` in the main context.

## Testing

```sh
ci/build.sh 1.30.5 /tmp/nginx-test
TEST_NGINX_BINARY=/tmp/nginx-test/sbin/nginx prove -r t/
ci/addresses.sh /tmp/nginx-test/sbin/nginx
ci/reload.sh /tmp/nginx-test/sbin/nginx
```

`t/` covers the configuration and the variables over IPv4; `ci/addresses.sh`
drives the IPv6 and dual stack paths, which Test::Nginx does not reach.

## License

BSD 2-Clause, see [LICENSE](LICENSE).
