# Changelog

## v0.1.0 (2026-10-20)

- First release: `$remote_addr_anon` and `$binary_remote_addr_anon`,
  prefix widths set by `ip_anon_v4_prefix` and `ip_anon_v6_prefix`.
- IPv4-mapped addresses are masked with the IPv4 prefix, so a dual stack
  listener does not log the client's four bytes.
- A prefix wider than its address family is rejected at configuration time.
- A connection without an IP address, a unix socket for instance, reports
  what `$remote_addr` and `$binary_remote_addr` report there instead of
  leaving the variable unset, so a limit key is never empty.
- The address is copied into `ngx_sockaddr_t` rather than a byte array,
  which keeps the alignment a `sockaddr_in6` needs.
