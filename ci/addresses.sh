#!/bin/sh
#
# Walk the module through every address family a connection can arrive on.
#
# The suite in t/ only ever sees 127.0.0.1, because that is where
# Test::Nginx points its client.  The branches that matter most are the
# other two: an IPv6 address, and an IPv4 address wearing an IPv6 sockaddr
# because the listener is dual stack.  The second one is the trap -- masking
# such an address with the IPv6 prefix would leave the four bytes that
# actually name the client untouched.
#
# usage: ci/addresses.sh <nginx binary>
#
# Set ADDRESSES_LOAD_MODULE to the path of ngx_http_ip_anon_module.so to
# test a loadable build.

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
}
CONF

cleanup() {
	[ -f "$WORK/logs/nginx.pid" ] &&
		kill -QUIT "$(cat "$WORK/logs/nginx.pid")" 2>/dev/null
	sleep 1
}
trap cleanup EXIT INT TERM

"$NGINX" -p "$WORK" -c "$WORK/conf/nginx.conf"
sleep 1

probe() {
	curl -s --max-time 5 "$1" || true
}

echo "--- IPv4 ---"
raw=$(probe "http://127.0.0.1:$PORT/raw")
anon=$(probe "http://127.0.0.1:$PORT/anon")
echo "  $raw -> $anon"
[ "$raw" = "127.0.0.1" ] || note "the unmasked address reads $raw, expected 127.0.0.1"
[ "$anon" = "127.0.0.0" ] || note "the masked address reads $anon, expected 127.0.0.0"

echo "--- IPv4 on a dual stack listener ---"
raw=$(probe "http://127.0.0.1:$DUAL/raw")
anon=$(probe "http://127.0.0.1:$DUAL/anon")
echo "  $raw -> $anon"
case $anon in
*127.0.0.0) ;;
*) note "a mapped IPv4 address came back as $anon, the last octet must be zero" ;;
esac

echo "--- IPv6 ---"
if curl -s --max-time 5 -o /dev/null "http://[::1]:$DUAL/anon" 2>/dev/null; then
	raw=$(probe "http://[::1]:$DUAL/raw")
	anon=$(probe "http://[::1]:$DUAL/anon")
	echo "  $raw -> $anon"
	[ "$raw" = "::1" ] || note "the unmasked address reads $raw, expected ::1"
	[ "$anon" = "::" ] || note "::1 masked to /48 must be ::, got $anon"
else
	echo "  no IPv6 on this host, skipped"
fi

echo "--- the error log ---"
if grep -qE '\[(alert|emerg)\]' "$WORK/logs/error.log"; then
	note "alert or emerg in the error log"
	grep -E '\[(alert|emerg)\]' "$WORK/logs/error.log" | head -3 >&2
else
	echo "  clean"
fi

[ "$fail" -eq 0 ] || { echo "ci/addresses.sh: failed" >&2; exit 1; }
echo "ci/addresses.sh: ok"
