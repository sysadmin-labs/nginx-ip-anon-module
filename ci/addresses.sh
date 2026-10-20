#!/bin/sh
#
# Drive the module over every address family a connection can arrive on.
#
# t/ only ever sees 127.0.0.1, because that is where Test::Nginx points its
# client.  The paths that matter are the other two: a native IPv6 address,
# and an IPv4 address in an IPv6 sockaddr from a dual stack listener.
#
# usage: ci/addresses.sh <nginx binary>
#
# Set ADDRESSES_LOAD_MODULE to the path of ngx_http_ip_anon_module.so to
# test a loadable build, ADDRESSES_REQUIRE_IPV6 to a non-empty value to
# fail instead of skipping where there is no IPv6 loopback.

set -eu

NGINX=${1:?path to the nginx binary missing}
WORK=${CI_WORK:-$(cd "$(dirname "$0")/.." && pwd)/ci-work}/addresses
PORT=${ADDRESSES_PORT:-18520}
DUAL=${ADDRESSES_DUAL_PORT:-18521}

command -v curl > /dev/null || { echo "curl(1) is needed" >&2; exit 2; }

rm -rf "$WORK"
mkdir -p "$WORK/conf" "$WORK/logs"

fail=0
note() {
	echo "  $1" >&2
	fail=1
}

load=
[ -n "${ADDRESSES_LOAD_MODULE:-}" ] &&
	load="load_module $ADDRESSES_LOAD_MODULE;"

cat > "$WORK/conf/nginx.conf" <<CONF
$load
daemon on;
master_process on;
worker_processes 1;
pid $WORK/logs/nginx.pid;
error_log $WORK/logs/error.log info;
events { worker_connections 64; }
http {
    access_log off;
    client_body_temp_path $WORK/tmp;

    ip_anon_v4_prefix 24;
    ip_anon_v6_prefix 48;

    server {
        listen 127.0.0.1:$PORT;
        location = /anon { return 200 "\$remote_addr_anon\n"; }
        location = /raw  { return 200 "\$remote_addr\n"; }
    }

    server {
        # dual stack on purpose: a client that connects over IPv4 arrives
        # here as ::ffff:127.0.0.1
        listen [::]:$DUAL ipv6only=off;
        location = /anon { return 200 "\$remote_addr_anon\n"; }
        location = /raw  { return 200 "\$remote_addr\n"; }
    }

    # a connection with no address to mask at all
    server {
        listen unix:$WORK/u.sock;
        location = /anon { return 200 "\$remote_addr_anon\n"; }
        location = /raw  { return 200 "\$remote_addr\n"; }
    }

    server {
        listen 127.0.0.1:$((PORT + 2));
        location / { proxy_pass http://unix:$WORK/u.sock:; }
    }
}
CONF

cleanup() {
	# runs from an EXIT trap under set -e, where a failing line would
	# abort the function and become the exit status of the script
	if [ -f "$WORK/logs/nginx.pid" ]; then
		kill -QUIT "$(cat "$WORK/logs/nginx.pid")" 2>/dev/null || true
	fi
	sleep 1
}
trap cleanup EXIT INT TERM

"$NGINX" -p "$WORK" -c "$WORK/conf/nginx.conf"
sleep 1

probe() {
	curl -s --max-time 5 "$1" || true
}

echo "== IPv4"
raw=$(probe "http://127.0.0.1:$PORT/raw")
anon=$(probe "http://127.0.0.1:$PORT/anon")
echo "  $raw -> $anon"
[ "$raw" = "127.0.0.1" ] || note "the unmasked address reads $raw, expected 127.0.0.1"
[ "$anon" = "127.0.0.0" ] || note "the masked address reads $anon, expected 127.0.0.0"

echo "== IPv4 in an IPv6 sockaddr, dual stack listener"
raw=$(probe "http://127.0.0.1:$DUAL/raw")
anon=$(probe "http://127.0.0.1:$DUAL/anon")
echo "  $raw -> $anon"
# exactly what nginx writes for a masked v4-mapped address, not just
# something ending in it
[ "$anon" = "::ffff:127.0.0.0" ] ||
	note "a mapped IPv4 address came back as $anon, expected ::ffff:127.0.0.0"

echo "== IPv6"
if curl -s --max-time 5 -o /dev/null "http://[::1]:$DUAL/anon" 2>/dev/null; then
	raw=$(probe "http://[::1]:$DUAL/raw")
	anon=$(probe "http://[::1]:$DUAL/anon")
	echo "  $raw -> $anon"
	[ "$raw" = "::1" ] || note "the unmasked address reads $raw, expected ::1"
	[ "$anon" = "::" ] || note "::1 masked to /48 must be ::, got $anon"
else
	if [ -n "${ADDRESSES_REQUIRE_IPV6:-}" ]; then
		note "no IPv6 loopback, and this run was told to require one"
	else
		echo "  no IPv6 on this host, skipped"
	fi
fi

echo "== unix socket, nothing to mask"
raw=$(probe "http://127.0.0.1:$((PORT + 2))/raw")
anon=$(probe "http://127.0.0.1:$((PORT + 2))/anon")
echo "  $raw -> $anon"
[ "$raw" = "unix:" ] || note "the unmasked value reads $raw, expected unix:"
[ "$anon" = "$raw" ] ||
	note "the masked value reads $anon, it has to match \$remote_addr ($raw)"

echo "== error log"
if grep -qE '\[(alert|emerg)\]' "$WORK/logs/error.log"; then
	note "alert or emerg in the error log"
	grep -E '\[(alert|emerg)\]' "$WORK/logs/error.log" | head -3 >&2
else
	echo "  clean"
fi

[ "$fail" -eq 0 ] || { echo "ci/addresses.sh: failed" >&2; exit 1; }
echo "ci/addresses.sh: ok"
