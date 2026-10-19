# Changelog

## v0.1.0 (2026-10-19)

- First release: `$remote_addr_anon` and `$binary_remote_addr_anon`,
  prefix widths set by `ip_anon_v4_prefix` and `ip_anon_v6_prefix`.
- IPv4-mapped addresses are masked with the IPv4 prefix, so a dual stack
  listener does not log the client's four bytes.
- A prefix wider than its address family is rejected at configuration time.
