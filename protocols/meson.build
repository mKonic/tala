wayland_scanner = find_program(dependency('wayland-scanner', native: true).get_variable('wayland_scanner'))
wl_protocol_dir = dependency('wayland-protocols').get_variable('pkgdatadir')

protocol_sources = []
foreach xml : [
  wl_protocol_dir / 'staging/ext-data-control/ext-data-control-v1.xml',
  meson.current_source_dir() / 'virtual-keyboard-unstable-v1.xml',
  meson.current_source_dir() / 'wlr-foreign-toplevel-management-unstable-v1.xml',
]
  base = xml.split('/')[-1].replace('.xml', '')
  protocol_sources += custom_target(base + '-client-header', input: xml, output: base + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  protocol_sources += custom_target(base + '-code', input: xml, output: base + '-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach
