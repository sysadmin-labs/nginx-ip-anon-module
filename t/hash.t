use Test::Nginx::Socket 'no_plan';

repeat_each(2);

run_tests();

__DATA__

=== TEST 1: a secret turns $remote_addr_hash into a stable pseudonym
--- config
    location = /t {
        ip_anon_hash_secret "testkey";
        return 200 $remote_addr_hash;
    }
--- request
GET /t
--- response_body chomp
f96ef97d8e045f15
--- no_error_log
[alert]



=== TEST 2: without a secret the pseudonym is absent
--- config
    location = /t {
        return 200 "[$remote_addr_hash]";
    }
--- request
GET /t
--- response_body chomp
[]
--- no_error_log
[alert]



=== TEST 3: ip_anon_hash_length sets the hex width
--- config
    location = /t {
        ip_anon_hash_secret "testkey";
        ip_anon_hash_length 8;
        return 200 $remote_addr_hash;
    }
--- request
GET /t
--- response_body chomp
f96ef97d
--- no_error_log
[alert]



=== TEST 4: a different secret yields a different pseudonym
--- config
    location = /t {
        ip_anon_hash_secret "otherkey";
        return 200 $remote_addr_hash;
    }
--- request
GET /t
--- response_body chomp
8f7abb93cefaeb8e
--- no_error_log
[alert]



=== TEST 5: a period keeps the width and stays stable within the window
--- config
    location = /t {
        ip_anon_hash_secret "testkey";
        ip_anon_hash_period 1h;
        return 200 $remote_addr_hash;
    }
--- request
GET /t
--- response_body_like: ^[0-9a-f]{16}$
--- no_error_log
[alert]



=== TEST 6: a length beyond the digest is rejected at configuration time
--- config
    ip_anon_hash_length 33;
    location = /t {
        return 200 ok;
    }
--- must_die
--- error_log
value must be between 1 and 32
