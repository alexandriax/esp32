#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
output="${TMPDIR:-/tmp}/moss-browser-app-test-$$"
trap 'rm -f "$output"' EXIT HUP INT TERM
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -Itests/browser_app_mocks tests/browser_app_test.cpp tests/browser_assets_stub.cpp \
  firmware/sloth_pet/browser_app.cpp firmware/sloth_pet/browser_ui.cpp firmware/sloth_pet/keyboard.cpp \
  firmware/sloth_pet/browser_url.cpp firmware/sloth_pet/browser_reader.cpp firmware/sloth_pet/browser_memory.cpp -o "$output"
"$output"
