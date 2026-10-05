#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out="$repo/build/lhp-browser/adapter-tests"
mkdir -p "$out"
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  "$repo/experiments/lhp_browser/tests/stripe_sink_test.cpp" -o "$out/stripe_sink_test"
"$out/stripe_sink_test"
