#!/usr/bin/env python3
"""Write the third-party notices for a firmware release (plain text).

Most licenses of the libraries in the firmware ask for their copyright notices
and license text to go with every binary copy (BSD-3-Clause, MIT, Apache-2.0
and the rest). The SBOM names the licenses but carries none of the texts, so
each release also gets this file.

What goes in is read from the linker map, not from a list kept by hand. Every
object file that put code or data into the image is traced to its source file
and to the component that source belongs to. Each component lists the
copyright lines found at the top of its linked source files, followed by its
license text. Where a source file's own header carries its license (code that
came into a library from elsewhere), that header is reproduced instead. An
object no component claims stops the script, so a library added to the build
cannot go out without its notices.

  controller      the controller image, from the maps of every controller
                  build the release carries, followed by the charger module
                  image it carries for programming blades
  charger-module  the blade image

Usage:
  notices.py controller --sdk ~/pico-sdk \\
      --map firmware/controller/build/controller.elf.map \\
      --map firmware/controller/build-pico2w/controller.elf.map \\
      --blade-map firmware/charger-module/build/charger-module.map \\
      --out controller-notices.txt
  notices.py charger-module --map firmware/charger-module/build/charger-module.map \\
      --out charger-module-notices.txt

The cross toolchain ships no license file for its C library, so newlib's and
libgloss's are kept in licenses/ next to this script, as published with
newlib 4.5.0.
"""

import argparse
import dataclasses
import pathlib
import re
import sys
import textwrap

TOOLS = pathlib.Path(__file__).resolve().parent
FIRMWARE = TOOLS.parent
REPO = FIRMWARE.parent
PROJECT_URL = "https://github.com/mikesmitty/power-manifold"
RULE = "-" * 72


@dataclasses.dataclass
class Component:
    name: str
    url: str
    license: str  # what it is used under, in words
    root: str  # where prefix and texts are found: "sdk", "build", "repo" or "tools"
    prefix: str  # the path under root that its sources sit in
    texts: list  # license files under root; empty when each file's header carries its license
    always: list = dataclasses.field(default_factory=list)  # headers compiled in, read for copyright lines


# The owner's guide lists the same components in plain words
# (docs/src/content/docs/guide/open-source.mdx): a component added here goes
# there too.
#
# The controller image. The first component whose prefix matches a source
# file claims it, so the pieces inside a library come before the library.
CONTROLLER = [
    Component("printf, in the Pico SDK", "https://github.com/mpaland/printf", "MIT",
              "sdk", "src/rp2_common/pico_printf/", []),
    Component("USB descriptors from MicroPython, in the Pico SDK", "https://github.com/micropython/micropython",
              "MIT", "sdk", "src/rp2_common/pico_stdio_usb/stdio_usb_descriptors.c", []),
    Component("SEGGER RTT, in the Pico SDK", "https://www.segger.com", "SEGGER's BSD-style license",
              "sdk", "src/rp2_common/pico_stdio_rtt/SEGGER/", []),
    Component("Pico SDK", "https://github.com/raspberrypi/pico-sdk", "BSD-3-Clause",
              "sdk", "src/", ["LICENSE.TXT"]),
    Component("Pico SDK", "https://github.com/raspberrypi/pico-sdk", "BSD-3-Clause",
              "build", "pico-sdk/", []),  # files the SDK generates in the build directory
    Component("lwIP", "https://github.com/lwip-tcpip/lwip", "BSD-3-Clause",
              "sdk", "lib/lwip/", ["lib/lwip/COPYING"]),
    Component("Mbed TLS", "https://github.com/Mbed-TLS/mbedtls",
              "Apache-2.0 (it is offered under Apache-2.0 or GPL-2.0-or-later)",
              "sdk", "lib/mbedtls/", ["lib/mbedtls/LICENSE"]),
    Component("cyw43-driver", "https://github.com/georgerobotics/cyw43-driver",
              "the Raspberry Pi license for Raspberry Pi silicon (LICENSE.RP)",
              "sdk", "lib/cyw43-driver/", ["lib/cyw43-driver/LICENSE.RP"]),
    Component("Rijndael (AES) by Philip J. Erdelsky, in BTstack", "http://www.efgh.com/software/rijndael.htm",
              "public domain", "sdk", "lib/btstack/3rd-party/rijndael/", []),
    Component("BTstack", "https://github.com/bluekitchen/btstack",
              "the Raspberry Pi license for BTstack on Raspberry Pi products (LICENSE.RP in the Pico SDK)",
              "sdk", "lib/btstack/", ["src/rp2_common/pico_btstack/LICENSE.RP"]),
    Component("TinyUSB", "https://github.com/hathach/tinyusb", "MIT",
              "sdk", "lib/tinyusb/", ["lib/tinyusb/LICENSE"]),
    Component("Monocypher", "https://monocypher.org",
              "BSD-2-Clause (it is offered under BSD-2-Clause or CC0-1.0)",
              "repo", "firmware/controller/lib/monocypher/", ["firmware/controller/lib/monocypher/LICENCE.md"]),
]

# The blade image. CMSIS core is headers only: its inline functions are
# compiled into the firmware's own objects, so no object of its own shows.
CHARGER_MODULE = [
    Component("CMSIS core", "https://github.com/STMicroelectronics/cmsis_core", "Apache-2.0",
              "build", "_deps/cmsis_core-src/", ["_deps/cmsis_device_g0-src/LICENSE.md"],
              always=["_deps/cmsis_core-src/Include/core_cm0plus.h", "_deps/cmsis_core-src/Include/cmsis_gcc.h"]),
    Component("CMSIS STM32G0 device files", "https://github.com/STMicroelectronics/cmsis_device_g0", "Apache-2.0",
              "build", "_deps/cmsis_device_g0-src/", ["_deps/cmsis_device_g0-src/LICENSE.md"]),
    Component("STM32G0 low-layer drivers", "https://github.com/STMicroelectronics/stm32g0xx_hal_driver",
              "BSD-3-Clause", "build", "_deps/stm32g0_ll-src/", ["_deps/stm32g0_ll-src/LICENSE.md"]),
    Component("ST USB Power Delivery core", "https://github.com/STMicroelectronics/stm32-mw-usbpd-core",
              "SLA0044, ST's license for use on ST microcontrollers only",
              "build", "_deps/usbpd_core-src/", ["_deps/usbpd_core-src/LICENSE.md"]),
    Component("ST USB Power Delivery device driver for STM32G0",
              "https://github.com/STMicroelectronics/stm32-mw-usbpd-device-g0",
              "SLA0044, ST's license for use on ST microcontrollers only",
              "build", "_deps/usbpd_device_g0-src/", ["_deps/usbpd_device_g0-src/LICENSE.md"]),
]

# The toolchain's libraries, by archive or object name.
NEWLIB = Component("newlib, the C library", "https://sourceware.org/newlib/",
                   "the newlib licenses (COPYING.NEWLIB)", "tools", "", ["licenses/COPYING.NEWLIB"])
LIBGLOSS = Component("libgloss, from newlib", "https://sourceware.org/newlib/",
                     "the libgloss licenses (COPYING.LIBGLOSS)", "tools", "", ["licenses/COPYING.LIBGLOSS"])
TOOLCHAIN = {
    **dict.fromkeys(["libc.a", "libc_nano.a", "libg.a", "libg_nano.a", "libm.a", "libm_nano.a"], NEWLIB),
    **dict.fromkeys(["libnosys.a", "librdimon.a", "crt0.o", "rdimon-crt0.o"], LIBGLOSS),
}
# GPL-3.0 with the GCC Runtime Library Exception, which asks for no notice
GCC_RUNTIME = {"libgcc.a", "crtbegin.o", "crtend.o", "crti.o", "crtn.o"}

# Sections that take no space in the image
NOT_LOADED = (".debug", ".comment", ".ARM.attributes", ".note", ".stab", ".gnu.attributes")
# What the linker makes itself: veneers and the like
LINKER_MADE = {"linker stubs"}


def linked_objects(map_path: pathlib.Path) -> set:
    """Input files that put code or data into the image, from a GNU ld map."""
    text = map_path.read_text(errors="replace")
    start = text.find("\nLinker script and memory map")
    if start < 0:
        sys.exit(f"{map_path}: not a GNU ld map")
    found, section = set(), None
    for line in text[start:].splitlines():
        m = re.match(r"^ (\.\S+|COMMON)\s*$", line)  # a long section name wraps
        if m:
            section = m.group(1)
            continue
        m = re.match(r"^ (\.\S+|COMMON)?\s*0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S.*)$", line)
        name = m and (m.group(1) or section)
        section = None
        if name and int(m.group(2), 16) and not name.startswith(NOT_LOADED):
            found.add(m.group(3).strip())
    found -= LINKER_MADE
    return found


def source_of(obj: str, build: pathlib.Path, source: pathlib.Path) -> pathlib.Path | None:
    """The source file behind a CMake object, or None for anything else."""
    m = re.match(r"^(.*?)CMakeFiles/[^/]+\.dir/(.+)\.(?:o|obj)$", obj)
    if not m:
        return None
    pre, rest = m.group(1), m.group(2)
    for p in (pathlib.Path("/" + rest), source / rest, build / rest, build / pre / rest):
        if p.is_file():
            return p.resolve()
    sys.exit(f"{obj}: no source file found for it")


def under(path: pathlib.Path, root: pathlib.Path, prefix: str) -> bool:
    try:
        rel = path.relative_to(root).as_posix()
    except ValueError:
        return False
    return rel == prefix or (prefix.endswith("/") and rel.startswith(prefix))


def clean(line: str) -> str:
    """A comment line without its comment marks."""
    s = line.strip()
    s = re.sub(r"^(?:/\*+|\*+/?|//+|#+|;+)", "", s)
    s = re.sub(r"(?:\*+/|\*+)$", "", s)
    return s.strip()


def copyright_lines(path: pathlib.Path) -> list:
    """The copyright lines near the top of a source file."""
    found = []
    for raw in path.read_text(errors="replace").splitlines()[:150]:
        s = " ".join(clean(raw).split())
        s = re.sub(r"^SPDX-FileCopyrightText:\s*", "Copyright ", s)
        if re.match(r"^(?:Copyright\b|\([cC]\)\s|©)", s) and "notice" not in s.lower():
            found.append(s)
    return found


def header(path: pathlib.Path) -> str:
    """The comment a source file starts with, without its comment marks or
    Doxygen commands, and without the paragraphs that describe the code."""
    lines, block, skip = [], False, False
    for raw in path.read_text(errors="replace").splitlines():
        s = raw.strip()
        end = False
        if block:
            end = "*/" in s
            s = s.split("*/")[0]
        elif s.startswith("/*"):
            block, s = True, s[2:]
            end = "*/" in s
            s = s.split("*/")[0]
        elif s.startswith("//"):
            s = s[2:]
        elif not s and not lines:
            continue
        else:
            break
        s = s.strip().strip("*/=").strip()
        if "END-OF-HEADER" in s:
            break
        tag = re.match(r"[\\@](\w+)\s*", s)  # a Doxygen command such as \author
        if tag:
            skip = tag.group(1) in ("brief", "details", "file")
            s = s[tag.end():]
        elif not s:
            skip = False  # a paragraph ends
        if skip:
            if end:
                break
            continue
        if s or (lines and lines[-1]):
            lines.append(s)
        if end:
            break
    return "\n".join(lines).strip()


class Image:
    """The components of one image and what each contributes to it."""

    def __init__(self, title: str, components: list, roots: dict, source_dir: pathlib.Path):
        self.title = title
        self.components = components
        self.roots = dict(roots)  # "build" follows the map being read
        self.source_dir = source_dir
        self.copyrights = {}  # component index -> copyright lines
        self.headers = {}  # component index -> distinct file headers
        self.toolchain = []  # toolchain components, in the order found
        self.gcc_runtime = False

    def add_map(self, map_path: pathlib.Path) -> None:
        self.roots["build"] = map_path.resolve().parent
        for obj in sorted(linked_objects(map_path)):
            self.claim(obj)

    def claim(self, obj: str) -> None:
        build = self.roots["build"]
        archive = re.match(r"^(.*)\(([^()]+)\)$", obj)
        file = pathlib.Path(archive.group(1) if archive else obj)
        src = None if archive else source_of(obj, build, self.source_dir)
        if src is None and file.name in GCC_RUNTIME:
            self.gcc_runtime = True
            return
        if src is None and file.name in TOOLCHAIN:
            if TOOLCHAIN[file.name] not in self.toolchain:
                self.toolchain.append(TOOLCHAIN[file.name])
            return

        path = src or (build / file).resolve()
        if under(path, FIRMWARE, "controller/src/") or under(path, FIRMWARE, "charger-module/src/"):
            return  # the firmware's own code
        for i, c in enumerate(self.components):
            if not under(path, self.roots[c.root], c.prefix):
                continue
            lines = self.copyrights.setdefault(i, set())
            if src is None:
                return  # a prebuilt library: its license file speaks for it
            lines.update(copyright_lines(src))
            h = header(src) if not c.texts else ""
            if h and h not in self.headers.setdefault(i, []):
                self.headers[i].append(h)
            return
        sys.exit(f"{obj}: no component claims {path}; add one to notices.py")

    def entries(self) -> list:
        """(component, copyright lines, headers) in the order the components are listed."""
        for i, c in enumerate(self.components):
            for h in c.always:
                p = self.roots[c.root] / h
                if not p.is_file():
                    sys.exit(f"{p}: missing")
                self.copyrights.setdefault(i, set()).update(copyright_lines(p))
        merged = {}
        for i, c in enumerate(self.components):
            if i not in self.copyrights:
                continue
            e = merged.setdefault((c.name, c.url), [c, set(), []])  # the Pico SDK has two roots
            e[1] |= self.copyrights[i]
            e[2] += [h for h in self.headers.get(i, []) if h not in e[2]]
        out = [(c, sorted(lines), headers) for c, lines, headers in merged.values()]
        return out + [(c, [], []) for c in self.toolchain]


def firmware_version(header_path: pathlib.Path) -> str:
    m = re.search(r'#\s*define\s+FW_VERSION\s+"([^"]+)"', header_path.read_text())
    if not m:
        sys.exit(f"{header_path}: no FW_VERSION")
    return m.group(1)


def license_text(path: pathlib.Path) -> str:
    """A license file's text. ST's SLA0044 Rev 6 file is Windows-1252, not
    UTF-8 (its quotes are single bytes), so a file that is not valid UTF-8 is
    read as that instead of losing those characters."""
    raw = path.read_bytes()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", errors="replace")


def write(out: pathlib.Path, title: str, intro: list, images: list) -> None:
    text = [title, "=" * len(title), ""] + intro
    seen = {}  # license text -> the component it was first given for
    for image in images:
        text += ["", "", image.title, "~" * len(image.title)]
        if image.gcc_runtime:
            text += ["", "This image also contains parts of the GCC runtime library (libgcc).",
                     "Its license, GPL-3.0 with the GCC Runtime Library Exception, asks",
                     "for no notice."]
        for c, lines, headers in image.entries():
            text += ["", RULE, c.name, c.url, f"Used under: {c.license}"]
            # leading blank lines go, the indentation of the first line stays
            bodies = [re.sub(r"^(?:[ \t]*\n)+", "", license_text(image.roots[c.root] / t)).rstrip()
                      for t in c.texts]
            # a file header carries its own copyright lines, and so may a license text
            lines = [l for l in lines if c.texts and not any(l in b for b in bodies)]
            if lines:
                text += [""] + lines
            for h in headers:
                text += ["", h]
            for body in bodies:
                if body in seen:
                    text += ["", f"License text: the same as for {seen[body]}, above."]
                else:
                    seen[body] = c.name
                    text += ["", body]
    out.write_text("\n".join(text) + "\n")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("product", choices=["controller", "charger-module"])
    ap.add_argument("--sdk", type=pathlib.Path, help="the pico-sdk the controller was built with")
    ap.add_argument("--map", type=pathlib.Path, action="append", required=True,
                    help="a linker map of the image; repeat it for every build of the image the release carries")
    ap.add_argument("--blade-map", type=pathlib.Path,
                    help="the map of the charger module image the controller carries")
    ap.add_argument("--out", required=True, type=pathlib.Path)
    args = ap.parse_args()

    roots = {"repo": REPO, "tools": TOOLS}
    images = []
    if args.product == "controller":
        if not args.sdk or not args.blade_map:
            ap.error("controller needs --sdk and --blade-map")
        version = firmware_version(FIRMWARE / "controller" / "src" / "manifold.h")
        title = f"Power Manifold controller firmware {version}: third-party notices"
        what = "The controller firmware"
        controller = Image("The controller image", CONTROLLER,
                           dict(roots, sdk=args.sdk.expanduser().resolve()), FIRMWARE / "controller")
        for m in args.map:
            controller.add_map(m)
        images.append(controller)
        blade_maps = [args.blade_map]
        blade_title = "The charger module image, carried for programming blades"
    else:
        version = firmware_version(FIRMWARE / "charger-module" / "src" / "blade.h")
        title = f"Power Manifold charger module firmware {version}: third-party notices"
        what = "The charger module firmware"
        blade_maps = args.map
        blade_title = "The charger module image"

    blade = Image(blade_title, CHARGER_MODULE, roots, FIRMWARE / "charger-module")
    for m in blade_maps:
        blade.add_map(m)
    images.append(blade)

    intro = textwrap.wrap(
        f"{what} contains the third-party software listed below. Each entry names the license "
        "it is used under, the copyright notices found in the source files the image is built "
        "from, and the license text.", 72)
    intro += [""] + textwrap.wrap(
        f"The firmware's own source code is published under the MIT License at {PROJECT_URL}.", 72)
    write(args.out, title, intro, images)


if __name__ == "__main__":
    main()
