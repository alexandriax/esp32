#!/usr/bin/env python3
"""Build the Moss browser application or isolated LHP probe. Never opens or flashes a device."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

from lhp_source_patches import apply_patches

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / 'experiments/lhp_browser/target'
HOST = ROOT / 'experiments/lhp_browser/host'
SDK_VERSION = 'idf-release_v5.5-b66b5448-v1'
TOOL_VERSION = '2411'
FQBN = 'esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashSize=16M,FlashMode=dio,PartitionScheme=custom'


def run(command, log):
    with log.open('w') as output:
        completed = subprocess.run([str(x) for x in command], stdout=output,
                                   stderr=subprocess.STDOUT)
    if completed.returncode:
        print(log.read_text()[-14000:], file=sys.stderr)
        raise RuntimeError(f'Build failed ({completed.returncode}); see {log}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application', action='store_true', help='Build the complete Moss application with the browser')
    parser.add_argument('--fetch', action='store_true', help='Allow downloading the pinned source archive when absent')
    parser.add_argument('--archive', type=Path,
                        help='Upstream tar.gz verified against host/source-pin.json; use host/probe.py fetch')
    parser.add_argument('--work', type=Path, default=None,
                        help='Dedicated output directory; must be empty or previously created by this tool')
    parser.add_argument('--arduino-data', type=Path, default=ROOT / '.tools/arduino-data')
    parser.add_argument('--arduino-cli', type=Path, default=ROOT / '.tools/arduino-cli')
    parser.add_argument('--toolchain', type=Path,
                        help='Optional path to riscv32-esp-elf toolchain bin directory')
    parser.add_argument('--sdk', type=Path,
                        help='Optional Arduino IDF5.5 esp32c6 precompiled SDK directory')
    parser.add_argument('--jobs', type=int, default=8)
    args = parser.parse_args()
    if args.jobs < 1 or args.jobs > 64:
        parser.error('--jobs must be between 1 and 64')
    work = (args.work or ROOT / 'build/lhp-browser' / ('application' if args.application else 'target')).resolve()
    data, cli = args.arduino_data.resolve(), args.arduino_cli.resolve()
    sdk = (args.sdk or data / f'packages/esp32/tools/esp32-arduino-libs/{SDK_VERSION}/esp32c6').resolve()
    toolchain = (args.toolchain or data / f'packages/esp32/tools/esp-rv32/{TOOL_VERSION}/bin').resolve()
    for required in (cli, data / 'packages/esp32/hardware/esp32/3.3.0/platform.txt',
                     sdk / 'flags/c_flags', toolchain / 'riscv32-esp-elf-gcc'):
        if not required.exists():
            parser.error(f'Missing installed Arduino 3.3.0 / IDF5.5 dependency: {required}')
    marker = work / '.moss-lhp-target-work'
    if work.exists() and any(work.iterdir()) and not marker.is_file():
        parser.error(f'Refusing to reuse unrelated nonempty output directory: {work}')
    work.mkdir(parents=True, exist_ok=True)
    marker.write_text('Moss isolated LHP target build output\n')
    spec = importlib.util.spec_from_file_location('moss_lhp_host_probe', HOST / 'probe.py')
    probe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(probe)
    archive = (args.archive or work / 'upstream.tar.gz').resolve()
    if not archive.exists() and args.fetch:
        from types import SimpleNamespace
        probe.fetch(SimpleNamespace(work=work))
        archive = work / 'upstream.tar.gz'
    if not archive.exists():
        parser.error('Pinned source archive missing; pass --archive or --fetch')
    source = probe.source_dir(work, archive)
    # Patch only the freshly extracted checksum-verified build source. Host
    # engine tests apply these same edits to their own private source copy.
    local_patches = apply_patches(source)
    lib_build = work / 'lws'

    # Match Arduino's RISC-V ABI and the configured IDF headers. Wwrite-strings
    # is Arduino's extra diagnostic; upstream uses a mutable pointer to a const
    # literal and its own Werror, so do not inject that non-upstream C warning.
    flags = (sdk / 'flags/c_flags').read_text().replace('-Wwrite-strings', '')
    flags += ' ' + (sdk / 'flags/defines').read_text()
    flags += ' -iprefix ' + shlex.quote(str(sdk / 'include') + '/')
    flags += ' ' + (sdk / 'flags/includes').read_text()
    flags += ' -I' + shlex.quote(str(sdk / 'dio_qspi/include'))
    flags += ' -DLWS_ESP_PLATFORM=1 -Os'
    toolchain_file = work / 'c6-toolchain.cmake'
    toolchain_file.write_text(
        'set(CMAKE_SYSTEM_NAME Generic)\nset(CMAKE_SYSTEM_PROCESSOR riscv32)\n'
        'set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)\n'
        f'set(CMAKE_C_COMPILER "{toolchain}/riscv32-esp-elf-gcc")\n'
        f'set(CMAKE_AR "{toolchain}/riscv32-esp-elf-ar")\n'
        f'set(CMAKE_RANLIB "{toolchain}/riscv32-esp-elf-ranlib")\n'
        f'set(CMAKE_C_FLAGS_INIT [=[{flags}]=])\n')
    options = {
        'LWS_WITH_ESP32': 'ON', 'LWS_WITH_MBEDTLS': 'ON', 'LWS_WITH_SSL': 'ON',
        'LWS_WITH_GENCRYPTO': 'ON',  # TLS certificate fingerprint dependency.
        'LWS_WITH_SHARED': 'OFF', 'LWS_WITH_STATIC': 'ON',
        'LWS_WITH_MINIMAL_EXAMPLES': 'OFF', 'LWS_WITHOUT_TESTAPPS': 'ON',
        'LWS_WITHOUT_SERVER': 'ON', 'LWS_WITH_LHP': 'ON',
        'LWS_WITH_LHP_UA_FLASH': 'ON', 'LWS_WITH_DLO': 'ON',
        # LHP currently references both codecs even for a text-only document.
        'LWS_WITH_UPNG': 'ON', 'LWS_WITH_JPEG': 'ON',
        'LWS_WITH_GIF': 'OFF', 'LWS_WITH_SVG': 'OFF', 'LWS_WITH_HL': 'OFF',
        'LWS_WITH_MD': 'OFF', 'LWS_WITH_JSONRPC': 'OFF', 'LWS_WITH_STUB': 'OFF',
        'LWS_WITH_PLUGINS': 'OFF', 'LWS_WITH_EVLIB_PLUGINS': 'OFF',
        'LWS_WITH_FILE_OPS': 'ON', 'LWS_ROLE_WS': 'OFF', 'LWS_WITH_HTTP2': 'OFF',
        'LWS_WITH_HTTP3': 'OFF', 'LWS_ROLE_QUIC': 'OFF', 'LWS_WITH_ZLIB': 'OFF',
        'LWS_WITH_CACHE_BLOB': 'OFF', 'LWS_WITH_SYS_ASYNC_DNS': 'OFF',
        'LWS_WITH_SYS_NTPCLIENT': 'OFF', 'LWS_WITH_TLS_SESSIONS': 'OFF',
        'LWS_WITH_SECURE_STREAMS': 'ON', 'LWS_WITH_SECURE_STREAMS_STATIC_POLICY_ONLY': 'OFF',
        'LWS_WITH_NETWORK': 'ON', 'LWS_WITH_UDP': 'OFF',
        'LWS_WITH_EXPORT_LWSTARGETS': 'OFF',
        'LWS_WITH_DRIVERS': 'OFF', 'LWS_WITH_SYS_SMD': 'OFF',
        'LWS_WITH_CONMON': 'OFF', 'LWS_WITH_WOL': 'OFF',
        'LWS_WITH_CACHE_NSCOOKIEJAR': 'OFF', 'LWS_WITH_DISKCACHE': 'OFF',
        'LWS_WITH_HTTP_BASIC_AUTH': 'OFF', 'LWS_WITH_HTTP_DIGEST_AUTH': 'OFF',
        'LWS_WITH_HTTP_UNCOMMON_HEADERS': 'OFF', 'LWS_WITH_SYS_STATE': 'ON',
        # STATIC_LIBRARY probes cannot detect missing link symbols. These
        # functions are absent from the target ABI / mbedTLS backend.
        'LWS_HAVE_MALLOC_TRIM': 'OFF', 'LWS_HAVE_MALLOC_USABLE_SIZE': 'OFF',
        'LWS_HAVE_PIPE2': 'OFF', 'LWS_HAVE_EXECVPE': 'OFF',
        'LWS_HAVE_PTHREAD_H': 'OFF', 'LWS_HAVE_TIMEGM': 'OFF',
        'LWS_HAVE_SSL_CTX_set_keylog_callback': 'OFF',
        'LWS_HAVE_SSL_SET_INFO_CALLBACK': 'OFF',
    }
    configure = ['cmake', '--fresh', '-S', source, '-B', lib_build,
                 f'-DCMAKE_TOOLCHAIN_FILE={toolchain_file}']
    configure += [f'-D{key}={value}' for key, value in options.items()]
    run(configure, work / 'configure.log')
    run(['cmake', '--build', lib_build, '--target', 'websockets',
         '--parallel', args.jobs], work / 'library.log')
    sketch_name = 'sloth_pet' if args.application else 'lhp_browser'
    stage = work / 'sketch' / sketch_name
    # This subtree is generated inside the marker-owned work directory.
    # Remove stale compilation units from earlier experiment revisions.
    if stage.exists():
        shutil.rmtree(stage)
    if args.application:
        shutil.copytree(ROOT / 'firmware/sloth_pet', stage)
    else:
        stage.mkdir(parents=True, exist_ok=True)
        for name in ('lhp_browser.ino', 'lhp_engine.c', 'lhp_engine.h'):
            shutil.copy2(TARGET / name, stage / name)
        shutil.copy2(ROOT / 'firmware/sloth_pet/browser_stripe_sink.h', stage / 'stripe_sink.h')
        for name in ('board.cpp', 'board.h', 'screen_rotation.h', 'storage_status.cpp',
                     'storage_status.h', 'storage_files.h', 'board_bus.h', 'partitions.csv'):
            shutil.copy2(ROOT / 'firmware/sloth_pet' / name, stage / name)
        vendor = stage / 'src/vendor'
        vendor.mkdir(parents=True, exist_ok=True)
        for name in ('esp_lcd_sh8601.c', 'esp_lcd_sh8601.h', 'LICENSE'):
            shutil.copy2(ROOT / 'firmware/sloth_pet/src/vendor' / name, vendor / name)
    config = work / 'arduino-cli.yaml'
    config.write_text('directories:\n  data: ' + json.dumps(str(data)) +
                     '\n  downloads: ' + json.dumps(str(work / 'downloads')) +
                     '\n  user: ' + json.dumps(str(work / 'arduino-user')) + '\n')
    includes = ' '.join('-I' + shlex.quote(str(path)) for path in
                        (source / 'include', lib_build, source / 'contrib/mcufont/fonts'))
    extra = '-DLWS_ESP_PLATFORM=1 ' + includes
    firmware = work / 'firmware'
    build = [cli, 'compile', '--config-file', config, '--fqbn', FQBN,
             '--build-property', 'upload.maximum_size=' + ('6291456' if args.application else '3145728'),
             '--build-property', 'compiler.c.extra_flags=' + extra,
             '--build-property', 'compiler.cpp.extra_flags=-DMOSS_DISPLAY_SPEED_OPT=1 ' + extra,
             '--build-property', 'compiler.libraries.ldflags=' + shlex.quote(str(lib_build / 'lib/libwebsockets.a')),
             '--build-property', 'compiler.c.elf.extra_flags=' + ('-Wl,--wrap=r_ble_ll_mem_generic_data_init,--wrap=esp_wifi_init' if args.application else ''),
             '--build-path', firmware, '--warnings', 'all', stage]
    run(build, work / 'firmware.log')
    image = firmware / (sketch_name + '.ino.bin')
    report = {'upstream': probe.PIN, 'local_patches': local_patches, 'fqbn': FQBN, 'arduino_core': '3.3.0',
              'sdk': SDK_VERSION, 'toolchain': TOOL_VERSION, 'flashed': False,
              'image_bytes': image.stat().st_size,
              'image_sha256': hashlib.sha256(image.read_bytes()).hexdigest(),
              'lws_heap_ceiling_bytes': 128 * 1024 if args.application else 192 * 1024,
              'browser_display_framebuffer': False, 'runtime_verified': False,
              'network': 'direct HTTP and verified HTTPS' if args.application else 'offline embedded fixture only'}
    (work / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print((work / 'firmware.log').read_text()[-2500:])
    print(f'Built {sketch_name}: {image}\nReport: {work / "build-report.json"}\nNo device was opened or flashed.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'lhp-target-build: {error}', file=sys.stderr)
        sys.exit(1)
