# Changelog

## v0.2.0 (2026-11-11)

- New `$remote_addr_hash`: a one-way pseudonym of the client address, keyed
  with `ip_anon_hash_secret`, so a log can tell two clients in one network
  apart and `limit_req` can act per client without carrying an address.
- `ip_anon_hash_period` mixes the current time window into the hash, so a
  token is stable within the window and unlinkable across windows.
- `ip_anon_hash_length` sets how many hex characters are emitted, up to 32.
- The pseudonym is HMAC-MD5, the one keyed hash nginx offers without OpenSSL,
  so the variable works in a build without SSL; keyed, it is a pseudonym and
  not a checksum. Without a secret the variable is absent, so it is opt-in.
- The full address is hashed, an IPv4-mapped address over its four IPv4
  bytes, so masking and the pseudonym stay independent.

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
