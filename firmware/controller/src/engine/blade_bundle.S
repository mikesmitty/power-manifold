// The gen-3 blade firmware, as built (CMake BLADE_IMAGE), for blade_bundle.c
    .section .rodata.blade_bundle, "a"
    .balign 4
    .global blade_bundle_start
    .global blade_bundle_end
blade_bundle_start:
    .incbin BLADE_BUNDLE_PATH
blade_bundle_end:
