#!/usr/bin/env python3
"""Apply reviewed renderer patches to a generated copy of the pinned LHP source."""
import argparse
from pathlib import Path
import shutil

PATCHES = ('renderer-only-no-gpio-isr', 'lwsac-bounded-allocator',
           'renderer-only-no-event-pipes', 'css-tables-in-flash')
EDITS = (
    ('lib/misc/lhp.c', 'static uint8_t css_lextable[]', 'static const uint8_t css_lextable[]'),
    ('lib/misc/lhp.c', 'static uint8_t css_propconst_lextable[]', 'static const uint8_t css_propconst_lextable[]'),
    ('lib/plat/freertos/freertos-init.c',
     '#if defined(LWS_ESP_PLATFORM)\n\tgpio_install_isr_service(0);\n#endif',
     '/* Moss renderer-only build: no global GPIO ISR service. */'),
    # CSS and glyph arenas must use the same allocator as their owners. Charge
    # the entire chunk, including unused capacity, against the renderer budget.
    ('lib/misc/lwsac/lwsac.c', '\tbf = malloc(alloc);',
     '\tbf = lws_malloc(alloc, "lwsac chunk");'),
    ('lib/misc/lwsac/lwsac.c', '\t\tfree(it);', '\t\tlws_free(it);'),
    # The browser supplies completed HTML and never services LWS networking.
    # On FreeRTOS, even its wakeup pipe creates two UDP sockets and persistent
    # caller-thread lwIP state. Avoid those unused resources on every platform.
    ('lib/core/context.c', '\tn = __lws_create_event_pipes(context);',
     '\t/* Moss renderer-only build: no network service or event pipes. */\n'
     '\tn = 0;'),
)
MARKER = '.moss-lhp-patched-copy'


def apply_patches(source):
    """Exact, idempotent edits; validate every match before writing any file."""
    changed = {}
    for relative, before, after in EDITS:
        path = source / relative
        content = changed.get(path, path.read_text())
        if content.count(before) == 1 and after not in content:
            content = content.replace(before, after)
        elif before in content or content.count(after) != 1:
            raise ValueError(f'LHP patch no longer matches the pinned source: {relative}')
        changed[path] = content
    for path, content in changed.items():
        if path.read_text() != content:
            path.write_text(content)
    return list(PATCHES)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--copy-to', required=True, type=Path,
                        help='Private build directory; the input source is never edited')
    args = parser.parse_args()
    source, destination = args.source.resolve(), args.copy_to.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        parser.error('Source and private copy must be separate directories')
    if destination.exists():
        if not (destination / MARKER).is_file():
            parser.error('Refusing to replace an unrecognized source directory')
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    (destination / MARKER).write_text('Moss browser renderer test source copy\n')
    # Match the verified archive extractor: omit symlinks and VCS metadata.
    # Upstream examples contain links back into the tree; following those
    # would recursively duplicate the entire source.
    def excluded(directory, names):
        return [name for name in names
                if name == '.git' or (Path(directory) / name).is_symlink()]
    shutil.copytree(source, destination, ignore=excluded, dirs_exist_ok=True)
    apply_patches(destination)


if __name__ == '__main__':
    main()
