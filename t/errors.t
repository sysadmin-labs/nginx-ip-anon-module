use Test::Nginx::Socket 'no_plan';

run_tests();

__DATA__

=== TEST 1: an IPv4 prefix above 32 is refused
--- config
    ip_anon_v4_prefix 33;
    location = /t {
        return 200 ok;
    }
--- must_die
--- error_log
value must be between 0 and 32



=== TEST 2: an IPv6 prefix above 128 is refused
--- config
    ip_anon_v6_prefix 129;
    location = /t {
        return 200 ok;
    }
--- must_die
--- error_log
value must be between 0 and 128



=== TEST 3: a prefix that is not a number is refused
--- config
    ip_anon_v4_prefix half;
    location = /t {
        return 200 ok;
    }
--- must_die
--- error_log
invalid number
