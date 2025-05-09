#!/usr/bin/env bash

##
# INFO level message
##
info() {
    echo -e -n "\e[32m$*\e[0m"
}

##
# WARN level message
##
warn() {
    echo -e -n "\e[33m$*\e[0m"
}

##
# ERROR level message
##
error() {
    echo -e -n "\e[31m$*\e[0m"
}

target_exec=$1
test_file=$2

declare -a test_cases
test_cases=${test_file:=$(find $(dirname $0)/tests -type f -name '*.amex')}

for test_case in ${test_cases[@]}; do
  info "Testing: $test_case..."
  expect="$(sed -nE 's/.*#.*expect: (.*)/\1/p' $test_case)"
  got="$($target_exec $test_case)"
  if ! [ "$expect" = "$got" ]; then
    error "\nexpect:\n"
    error "$expect\n"
    error "\ngot:\n"
    error "$got\n"
  else
    info "  ok\n"
  fi
done
