#!/usr/bin/env bats

@test "stdin: inout" {
    TEST="abc123"
    run hexlog inout cat -n <<<"$TEST"
    expect='     1	abc123
61 62 63 31 32 33 0A                              |abc123.| (0)
20 20 20 20 20 31 09 61  62 63 31 32 33 0A        |     1.abc123.| (1)'
    cat << EOF
--- output
$output
===
$expect
--- output
EOF

    [ "$status" -eq 0 ]
    [ "$output" = "$expect" ]
}

@test "stdin: in" {
    TEST="abc123"
    run hexlog in cat -n <<<"$TEST"
    expect='     1	abc123
61 62 63 31 32 33 0A                              |abc123.| (0)'
    cat << EOF
--- output
$output
===
$expect
--- output
EOF

    [ "$status" -eq 0 ]
    [ "$output" = "$expect" ]
}

@test "stdin: out" {
    TEST="abc123"
    run hexlog out cat -n <<<"$TEST"
    expect='     1	abc123
20 20 20 20 20 31 09 61  62 63 31 32 33 0A        |     1.abc123.| (1)'
    cat << EOF
--- output
$output
===
$expect
--- output
EOF

    [ "$status" -eq 0 ]
    [ "$output" = "$expect" ]
}

@test "stdin: inout: label" {
    TEST="abc123"
    export HEXLOG_LABEL_STDIN="<"
    export HEXLOG_LABEL_STDOUT=">"
    run hexlog inout cat -n <<<"$TEST"
    expect='     1	abc123
61 62 63 31 32 33 0A                              |abc123.|<
20 20 20 20 20 31 09 61  62 63 31 32 33 0A        |     1.abc123.|>'
    cat << EOF
--- output
$output
===
$expect
--- output
EOF

    [ "$status" -eq 0 ]
    [ "$output" = "$expect" ]
}

@test "stdio: process restrictions" {
    run sh -c "hexlog inout echo test >/dev/null"
    [ "$status" -eq 0 ]

    run sh -c "hexlog inout echo test >/dev/null </dev/null"
    [ "$status" -eq 0 ]
}
