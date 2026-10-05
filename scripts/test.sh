#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host
python3 scripts/generate_humor.py --check
c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/pet_humor_test.cpp firmware/sloth_pet/pet_humor.cpp -o build/host/pet_humor
build/host/pet_humor
c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/humor_bank_test.cpp firmware/sloth_pet/pet_humor.cpp firmware/sloth_pet/humor_bank.cpp \
  firmware/sloth_pet/pet_speech.cpp -o build/host/humor_bank
build/host/humor_bank
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/pet_speech_test.cpp firmware/sloth_pet/pet_speech.cpp firmware/sloth_pet/pet_renderer.cpp \
  firmware/sloth_pet/pet_settings.cpp firmware/sloth_pet/local_time.cpp -o build/host/pet_speech
build/host/pet_speech
for test in pet_reaction pet_state pet_record pet_timeline elapsed_care rtc_calendar eastern_time gestures display_idle display_controls volume_overlay remote_connection touch_command forest_fidget_controls tetris_controls ui_icons; do
  c++ -std=c++11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined "tests/${test}_test.cpp" -o "build/host/$test"
  "build/host/$test"
done
cc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -c firmware/sloth_pet/src/vendor/lz4/lz4.c -o build/host/lz4-test.o
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/display_stream_test.cpp firmware/sloth_pet/display_stream.cpp build/host/lz4-test.o -o build/host/display_stream
build/host/display_stream
c++ -std=c++11 -O1 -Wall -Wextra -Werror -Wno-unused-function -pedantic -fsanitize=address,undefined \
  tests/jpeg_display_test.cpp firmware/sloth_pet/jpeg_display.cpp \
  firmware/sloth_pet/src/vendor/jpegdec/JPEGDEC.cpp -o build/host/jpeg_display
UBSAN_OPTIONS=halt_on_error=1 build/host/jpeg_display
python3 tests/sync_clock_test.py
python3 tests/monitor_test.py
python3 tests/browser_device_check_test.py
for test in pet_settings local_time; do
  c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
    "tests/${test}_test.cpp" firmware/sloth_pet/pet_settings.cpp \
    firmware/sloth_pet/local_time.cpp -o "build/host/$test"
  "build/host/$test"
done
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/settings_ui_test.cpp firmware/sloth_pet/settings_ui.cpp firmware/sloth_pet/keyboard.cpp \
  firmware/sloth_pet/pet_settings.cpp firmware/sloth_pet/local_time.cpp -o build/host/settings_ui
build/host/settings_ui
for test in menu_ui pong_game leaf_sweep forest_fidget game_audio game_records radio_explorer; do
  c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
    "tests/${test}_test.cpp" "firmware/sloth_pet/${test}.cpp" -o "build/host/$test"
  "build/host/$test"
done
# Passive radio discovery uses threaded SDK mocks, never real RF hardware.
c++ -std=c++11 -O2 -pthread -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -Itests/radio_explorer_mocks tests/radio_explorer_driver_test.cpp \
  firmware/sloth_pet/radio_explorer_driver.cpp firmware/sloth_pet/radio_explorer.cpp \
  -o build/host/radio_explorer_driver
build/host/radio_explorer_driver
# Credential entry is shared by Remote Display and the standalone network picker.
for test in remote_display_ui wifi_networks_ui; do
  c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
    "tests/${test}_test.cpp" "firmware/sloth_pet/${test}.cpp" \
    firmware/sloth_pet/wifi_network_editor.cpp firmware/sloth_pet/keyboard.cpp -o "build/host/$test"
  "build/host/$test"
done
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/tetris_game_test.cpp firmware/sloth_pet/tetris_game.cpp firmware/sloth_pet/tetris_renderer.cpp -o build/host/tetris_game
build/host/tetris_game
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/tetris_render_test.cpp firmware/sloth_pet/tetris_game.cpp firmware/sloth_pet/tetris_renderer.cpp -o build/host/tetris_render
build/host/tetris_render
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/game_overlay_test.cpp firmware/sloth_pet/game_overlay.cpp firmware/sloth_pet/keyboard.cpp firmware/sloth_pet/game_records.cpp \
  -o build/host/game_overlay
build/host/game_overlay
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -Itests/sensors_mocks -Ifirmware/sloth_pet \
  tests/sensors_test.cpp firmware/sloth_pet/sensors.cpp -o build/host/sensors
build/host/sensors
# Feed real CST9220 packet semantics through every keyboard family and controls.
c++ -std=c++11 -O1 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -Itests/sensors_mocks -Ifirmware/sloth_pet tests/touch_ui_integration_test.cpp \
  firmware/sloth_pet/sensors.cpp firmware/sloth_pet/browser_ui.cpp \
  firmware/sloth_pet/wifi_networks_ui.cpp firmware/sloth_pet/wifi_network_editor.cpp \
  firmware/sloth_pet/remote_display_ui.cpp firmware/sloth_pet/settings_ui.cpp firmware/sloth_pet/keyboard.cpp \
  firmware/sloth_pet/pet_settings.cpp firmware/sloth_pet/local_time.cpp \
  firmware/sloth_pet/menu_ui.cpp firmware/sloth_pet/game_overlay.cpp \
  firmware/sloth_pet/game_records.cpp -o build/host/touch_ui_integration
build/host/touch_ui_integration
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -Itests/rtc_mocks tests/rtc_driver_test.cpp firmware/sloth_pet/pet_rtc.cpp \
  -o build/host/rtc_driver
build/host/rtc_driver
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -Itests/power_mocks tests/pet_power_test.cpp firmware/sloth_pet/pet_power.cpp \
  -o build/host/pet_power
build/host/pet_power
c++ -std=c++11 -Wall -Wextra -Werror -Wno-unused-function -pedantic -fsanitize=address,undefined \
  -Itests/board_mocks tests/board_display_test.cpp firmware/sloth_pet/board.cpp \
  firmware/sloth_pet/jpeg_display.cpp firmware/sloth_pet/src/vendor/jpegdec/JPEGDEC.cpp \
  -o build/host/board_display
build/host/board_display
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -Ifirmware/sloth_pet tests/board_bus_test.cpp -o build/host/board_bus
build/host/board_bus
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -Itests/storage_mocks tests/storage_status_test.cpp firmware/sloth_pet/storage_status.cpp \
  -o build/host/storage_status
build/host/storage_status
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/pet_renderer_test.cpp firmware/sloth_pet/pet_renderer.cpp firmware/sloth_pet/pet_speech.cpp \
  firmware/sloth_pet/pet_settings.cpp firmware/sloth_pet/local_time.cpp -o build/host/pet_renderer
build/host/pet_renderer
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tools/render_preview.cpp firmware/sloth_pet/pet_renderer.cpp firmware/sloth_pet/pet_speech.cpp \
  firmware/sloth_pet/pet_settings.cpp firmware/sloth_pet/local_time.cpp -o build/host/render_preview
for state in idle feed play sleep dance; do
  build/host/render_preview "build/host/${state}.ppm" "$state"
done
for scene in nyc space island undersea; do
  build/host/render_preview "build/host/scene-${scene}.ppm" idle 1000 charging sloth "$scene"
done
for reaction in 0 1 2 3 4 5 6 7 8 9 10 11; do
  build/host/render_preview "build/host/reaction-${reaction}.ppm" idle 900 charging sloth jungle Moss show \
    'Nap first. Questions later.' "$reaction" 900
done
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tools/settings_preview.cpp firmware/sloth_pet/settings_ui.cpp firmware/sloth_pet/keyboard.cpp \
  firmware/sloth_pet/pet_settings.cpp firmware/sloth_pet/local_time.cpp \
  firmware/sloth_pet/menu_ui.cpp -o build/host/settings_preview
for page in main main-bottom storage storage-checking storage-empty storage-unavailable storage-error animal name numbers zone zone-last scene scene-last undersea notice maxname motion motion-triggered motion-offline display display-waiting display-permission display-lost menu menu-bottom menu-back games games-fidget games-back pong pong-settings tetris tetris-settings leaf-sweep leaf-sweep-settings icons icons-selected; do
  build/host/settings_preview "build/host/settings-${page}.ppm" "$page"
done

c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tools/utility_preview.cpp firmware/sloth_pet/radio_explorer.cpp \
  firmware/sloth_pet/menu_ui.cpp -o build/host/utility_preview
for page in menu utilities wifi ble detail ble-detail ble-data direction empty error; do
  build/host/utility_preview "build/host/utility-${page}.ppm" "$page"
done

# Optional audio and persistent wireless pairing: mocks never access real NVS/I2S.
c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/audio_buffer_test.cpp -o build/host/audio_buffer
build/host/audio_buffer
c++ -std=c++17 -pthread -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -Itests/audio_mocks tests/audio_output_test.cpp firmware/sloth_pet/audio_output.cpp -o build/host/audio_output
build/host/audio_output
c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -Itests/wifi_store_mocks -Ifirmware/sloth_pet tests/paired_wifi_store_test.cpp \
  firmware/sloth_pet/paired_wifi_store.cpp -lz -o build/host/paired_wifi_store
build/host/paired_wifi_store

c++ -std=c++11 -Wall -Wextra -Werror -pedantic \
  tools/tetris_preview.cpp firmware/sloth_pet/tetris_game.cpp firmware/sloth_pet/tetris_renderer.cpp -o build/host/tetris_preview
for state in falling stack game-over metrics pressed-left pressed-right pressed-rotate pressed-drop pressed-pause next-i next-o next-t next-s next-z next-j next-l; do
  build/host/tetris_preview "build/host/tetris-${state}.ppm" "$state"
done
c++ -std=c++11 -Wall -Wextra -Werror -pedantic \
  tools/game_overlay_preview.cpp firmware/sloth_pet/game_overlay.cpp firmware/sloth_pet/keyboard.cpp firmware/sloth_pet/game_records.cpp \
  -o build/host/game_overlay_preview
for page in confirm name name-middle name-bottom pong pong2 tetris tetris2 leaf leaf2; do
  build/host/game_overlay_preview "build/host/game-${page}.ppm" "$page"
done

c++ -std=c++11 -Wall -Wextra -Werror -pedantic \
  tools/leaf_sweep_preview.cpp firmware/sloth_pet/leaf_sweep.cpp -o build/host/leaf_sweep_preview
for state in ready sweeping cans game-over; do
  build/host/leaf_sweep_preview "build/host/leaf-sweep-${state}.ppm" "$state"
done

c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/forest_fidget_render_test.cpp firmware/sloth_pet/forest_fidget.cpp \
  firmware/sloth_pet/forest_fidget_renderer.cpp -o build/host/forest_fidget_render
build/host/forest_fidget_render
c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic \
  tools/forest_fidget_preview.cpp firmware/sloth_pet/forest_fidget.cpp \
  firmware/sloth_pet/forest_fidget_renderer.cpp -o build/host/forest_fidget_preview
for toy in 0 1 2 3 4 5 6 7 8 9 10 11; do
  build/host/forest_fidget_preview "build/host/forest-fidget-${toy}.ppm" "$toy"
  build/host/forest_fidget_preview "build/host/forest-fidget-${toy}-active.ppm" "$toy" active
done

# Shared Wi-Fi network picker previews: no radio, credentials, or device access.
c++ -std=c++11 -Wall -Wextra -Werror -pedantic \
  tools/wifi_networks_preview.cpp firmware/sloth_pet/wifi_networks_ui.cpp \
  firmware/sloth_pet/wifi_network_editor.cpp firmware/sloth_pet/keyboard.cpp -o build/host/wifi_networks_preview
for page in list bottom password network forget waiting connecting connected failed empty; do
  build/host/wifi_networks_preview "build/host/wifi-${page}.ppm" "$page"
done

# Shared station service policy and credential migration; mocks never start Wi-Fi.
c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/wifi_networks_test.cpp firmware/sloth_pet/wifi_networks_core.cpp -o build/host/wifi_networks
build/host/wifi_networks
c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -Itests/wifi_store_mocks tests/wifi_network_store_test.cpp \
  firmware/sloth_pet/wifi_network_store.cpp firmware/sloth_pet/paired_wifi_store.cpp \
  -o build/host/wifi_network_store
build/host/wifi_network_store

c++ -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -Itests/wifi_networks_mocks tests/wifi_networks_driver_test.cpp \
  firmware/sloth_pet/wifi_networks.cpp firmware/sloth_pet/wifi_networks_core.cpp \
  -o build/host/wifi_networks_driver
build/host/wifi_networks_driver

# Browser UI, URL/HTTP bounds and real async fetch logic against SDK mocks.
# These tests never open a device or contact a website.
for test in keyboard browser_ui browser_url browser_http; do
  keyboard_source=firmware/sloth_pet/keyboard.cpp
  if [ "$test" = keyboard ]; then keyboard_source=; fi
  c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
    "tests/${test}_test.cpp" "firmware/sloth_pet/${test}.cpp" $keyboard_source -o "build/host/$test"
  "build/host/$test"
done
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -Itests/browser_fetch_mocks tests/browser_fetch_test.cpp tests/browser_assets_stub.cpp \
  firmware/sloth_pet/browser_fetch.cpp firmware/sloth_pet/browser_http.cpp \
  firmware/sloth_pet/browser_url.cpp firmware/sloth_pet/browser_reader.cpp firmware/sloth_pet/browser_memory.cpp \
  firmware/sloth_pet/keyboard.cpp -o build/host/browser_fetch
build/host/browser_fetch
scripts/test-browser-app.sh
c++ -std=c++11 -Wall -Wextra -Werror -pedantic \
  tools/browser_preview.cpp firmware/sloth_pet/browser_ui.cpp firmware/sloth_pet/keyboard.cpp -o build/host/browser_preview
for page in ready loading error keyboard keyboard-end page; do
  build/host/browser_preview "build/host/browser-${page}.ppm" "$page"
done

# The browser borrows the canvas without releasing it to the system allocator.
c++ -std=c++11 -pthread -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/browser_memory_test.cpp firmware/sloth_pet/browser_memory.cpp -o build/host/browser_memory
build/host/browser_memory
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/browser_reader_test.cpp firmware/sloth_pet/browser_reader.cpp firmware/sloth_pet/browser_url.cpp \
  firmware/sloth_pet/browser_memory.cpp firmware/sloth_pet/keyboard.cpp -o build/host/browser_reader
build/host/browser_reader

# Foreground resource ownership and timestamp-based background care.
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/app_runtime_test.cpp -o build/host/app_runtime
build/host/app_runtime

c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -Itests/wifi_networks_mocks tests/wifi_memory_test.cpp firmware/sloth_pet/wifi_memory.cpp \
  -o build/host/wifi_memory
build/host/wifi_memory

c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  tests/screen_rotation_test.cpp -o build/host/screen_rotation
build/host/screen_rotation
