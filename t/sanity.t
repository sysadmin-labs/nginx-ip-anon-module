use Test::Nginx::Socket 'no_plan';

repeat_each(2);

run_tests();

__DATA__

=== TEST 1: the default drops the last octet
--- config
    location = /t {
        return 200 $remote_addr_anon;
    }
--- request
GET /t
--- response_body chomp
127.0.0.0
--- no_error_log
[alert]



=== TEST 2: a prefix of 32 keeps the address
--- config
    ip_anon_v4_prefix 32;
    location = /t {
        return 200 $remote_addr_anon;
    }
--- request
GET /t
--- response_body chomp
127.0.0.1
--- no_error_log
[alert]



=== TEST 3: a prefix of 0 drops the address entirely
--- config
    ip_anon_v4_prefix 0;
    location = /t {
        return 200 $remote_addr_anon;
    }
--- request
GET /t
--- response_body chomp
0.0.0.0
--- no_error_log
[alert]



=== TEST 4: the prefix is applied bit by bit, not byte by byte
--- config
    ip_anon_v4_prefix 4;
    location = /t {
        return 200 $remote_addr_anon;
    }
--- request
GET /t
--- response_body chomp
112.0.0.0
--- no_error_log
[alert]



=== TEST 5: the binary form carries the masked bytes
--- config
    location = /t {
        return 200 $binary_remote_addr_anon;
    }
--- request
GET /t
--- response_body eval
"\x7f\x00\x00\x00"
--- no_error_log
[alert]



=== TEST 6: the unmasked address is untouched
--- config
    location = /t {
        return 200 "$remote_addr $remote_addr_anon";
    }
--- request
GET /t
--- response_body chomp
127.0.0.1 127.0.0.0
--- no_error_log
[alert]
