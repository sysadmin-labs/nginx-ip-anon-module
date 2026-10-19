use Test::Nginx::Socket 'no_plan';

repeat_each(2);

run_tests();

__DATA__

=== TEST 1: a setting in the server block reaches the location
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



=== TEST 2: the location wins over the server block
--- config
    ip_anon_v4_prefix 4;
    location = /t {
        ip_anon_v4_prefix 32;
        return 200 $remote_addr_anon;
    }
--- request
GET /t
--- response_body chomp
127.0.0.1
--- no_error_log
[alert]



=== TEST 3: the variable works in a log format
--- http_config
    log_format anon '$remote_addr_anon';
--- config
    access_log off;
    location = /t {
        return 200 ok;
    }
--- request
GET /t
--- response_body chomp
ok
--- no_error_log
[alert]
