# Changelog

Newest first.  Dates are release dates.

## v0.1.0 (2026-10-19)

### Added

- First release.  `$remote_addr_anon` and `$binary_remote_addr_anon` carry
  the client address with its host part masked off, and
  `ip_anon_v4_prefix` and `ip_anon_v6_prefix` say how much of it is kept.

- An IPv4 address that arrives in an IPv6 sockaddr, which is what a dual
  stack listener produces, is masked with the IPv4 prefix.  Masking it with
  the IPv6 prefix would zero part of the `::ffff:` marker and leave the
  client's own four bytes in the log, which is the mistake this module
  exists to avoid.

- A prefix wider than its address family is refused at configuration time.

### Why this module exists

The one module that did this before, `masonicboom/ipscrub`, had its last
commit in 2018 and hashes the address instead of masking it, which answers
a different question.  nginx itself has had an open ticket for a shortened
address since 2015 (trac #868).
