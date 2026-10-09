#!/usr/bin/env python3
"""Write the software bill of materials for a firmware release (CycloneDX 1.6 JSON).

The Cyber Resilience Act asks for an SBOM covering at least a product's
top-level dependencies (Regulation (EU) 2024/2847, Annex I, Part II, point 1).
This lists what each image is built from, with versions read from the sources
the build used, not from a list kept by hand:

  controller      the controller image: the pico-sdk and the parts of it that
                  are linked (versions from their own headers), Monocypher
                  (vendored), the C library of the cross toolchain, and the
                  charger module image it carries, with that image's own
                  dependencies
  charger-module  the blade image: CMSIS and ST's USB-PD stack at the tags
                  pinned in firmware/charger-module/cmake, and the C library

Usage:
  sbom.py controller --sdk ~/pico-sdk --out controller.cdx.json
  sbom.py charger-module --out charger-module.cdx.json

The output is reproducible: the timestamp is the commit time of HEAD and the
serial number is derived from the product name and version.
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys
import uuid

FIRMWARE = pathlib.Path(__file__).resolve().parent.parent
REPO = FIRMWARE.parent
SUPPLIER = {"name": "Idle Curiosity LLC"}
PROJECT_URL = "https://github.com/mikesmitty/power-manifold"

# The pico-sdk libraries the controller links (firmware/controller/CMakeLists.txt;
# CI checks out exactly these submodules). Each entry: directory under lib/,
# component name, GitHub owner/repo, license, how to read the version, and the
# project's tag for that version ({v} the version, {u} with underscores).
SDK_LIBS = [
    ("lwip", "lwip", "lwip-tcpip/lwip", "BSD-3-Clause",
     ("src/include/lwip/init.h", ["LWIP_VERSION_MAJOR", "LWIP_VERSION_MINOR", "LWIP_VERSION_REVISION"]), "STABLE-{u}_RELEASE"),
    ("mbedtls", "mbedtls", "Mbed-TLS/mbedtls", "Apache-2.0 OR GPL-2.0-or-later",
     ("include/mbedtls/build_info.h", "MBEDTLS_VERSION_STRING"), "v{v}"),
    ("cyw43-driver", "cyw43-driver", "georgerobotics/cyw43-driver", None,
     ("src/cyw43.h", ["CYW43_VERSION_MAJOR", "CYW43_VERSION_MINOR", "CYW43_VERSION_MICRO"]), "v{v}"),
    ("btstack", "btstack", "bluekitchen/btstack", None,
     ("src/btstack_version.h", "BTSTACK_VERSION_STRING"), "v{v}"),
    ("tinyusb", "tinyusb", "hathach/tinyusb", "MIT",
     ("src/tusb_option.h", ["TUSB_VERSION_MAJOR", "TUSB_VERSION_MINOR", "TUSB_VERSION_REVISION"]), "{v}"),
]

# Licenses that are not SPDX identifiers are given by name.
NAMED_LICENSES = {
    "cyw43-driver": "cyw43-driver license (LICENSE and LICENSE.RP in the driver)",
    "btstack": "BTstack license with the Raspberry Pi exemption (LICENSE in BTstack)",
}

# The charger module's fetched dependencies, by repository name.
ST_LICENSES = {
    "cmsis_core": "Apache-2.0",
    "cmsis_device_g0": "Apache-2.0",
    "stm32g0xx_hal_driver": "BSD-3-Clause",
    "stm32-mw-usbpd-core": None,
    "stm32-mw-usbpd-device-g0": None,
}
ST_NAMED = "SLA0044 (ST Ultimate Liberty license, for use on ST microcontrollers only)"


def define(path: pathlib.Path, names) -> str:
    """A version from a C header: one string macro, or numeric parts joined by dots."""
    text = path.read_text(errors="replace")

    def value(name: str) -> str:
        m = re.search(rf"#\s*define\s+{name}\s+\"?([^\"\s]+)\"?", text)
        if not m:
            sys.exit(f"{path}: no {name}")
        return m.group(1)

    if isinstance(names, str):
        return value(names)
    return ".".join(value(n) for n in names)


def licenses(spdx: str | None, named: str | None) -> list:
    if spdx:
        return [{"expression": spdx}]
    return [{"license": {"name": named}}]


def github_component(name, version, repo, spdx, named, ref=None) -> dict:
    ref = ref or version
    return {
        "type": "library",
        "bom-ref": f"{name}@{version}",
        "name": name,
        "version": version,
        "licenses": licenses(spdx, named),
        "purl": f"pkg:github/{repo}@{ref}",
        "externalReferences": [{"type": "vcs", "url": f"https://github.com/{repo}"}],
    }


def sdk_version(sdk: pathlib.Path) -> str:
    """MAJOR.MINOR.REVISION from the set() lines of pico_sdk_version.cmake."""
    path = sdk / "pico_sdk_version.cmake"
    text = path.read_text()
    parts = []
    for p in ("MAJOR", "MINOR", "REVISION"):
        m = re.search(rf"set\(PICO_SDK_VERSION_{p} (\d+)\)", text)
        if not m:
            sys.exit(f"{path}: no PICO_SDK_VERSION_{p}")
        parts.append(m.group(1))
    return ".".join(parts)


def firmware_version(header: pathlib.Path) -> str:
    return define(header, "FW_VERSION")


def newlib() -> dict | None:
    """The C library the cross toolchain links in, versioned from its newlib.h."""
    try:
        out = subprocess.run(
            ["arm-none-eabi-gcc", "-E", "-dM", "-include", "newlib.h", "-x", "c", "-"],
            input="", capture_output=True, text=True, check=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError):
        print("sbom: arm-none-eabi-gcc not found; the C library is left out", file=sys.stderr)
        return None
    m = re.search(r"#define _NEWLIB_VERSION \"([^\"]+)\"", out)
    if not m:
        print("sbom: newlib.h has no _NEWLIB_VERSION; the C library is left out", file=sys.stderr)
        return None
    version = m.group(1)
    return {
        "type": "library",
        "bom-ref": f"newlib@{version}",
        "name": "newlib",
        "version": version,
        "description": "C library of the arm-none-eabi toolchain",
        "licenses": [{"license": {"name": "newlib licenses (BSD-style; COPYING.NEWLIB)"}}],
        "externalReferences": [{"type": "website", "url": "https://sourceware.org/newlib/"}],
    }


def fetched_components() -> list:
    """FetchContent_Declare blocks in firmware/charger-module/cmake: repository and pinned tag."""
    found = []
    for f in sorted((FIRMWARE / "charger-module" / "cmake").glob("*.cmake")):
        for block in re.findall(r"FetchContent_Declare\((.*?)\)", f.read_text(), re.S):
            repo = re.search(r"GIT_REPOSITORY\s+https://github\.com/(\S+)", block)
            tag = re.search(r"GIT_TAG\s+(\S+)", block)
            if not repo or not tag:
                sys.exit(f"{f}: a FetchContent_Declare without a GitHub repository and tag")
            repo = repo.group(1).removesuffix(".git")
            name = repo.split("/")[1]
            if name not in ST_LICENSES:
                sys.exit(f"{f}: no license known for {name}; add it to ST_LICENSES")
            version = tag.group(1).removeprefix("v")
            found.append(github_component(name, version, repo, ST_LICENSES[name], ST_NAMED, tag.group(1)))
    return found


def charger_module(c_library: dict | None) -> tuple[dict, list, list]:
    """The blade image as a component, its dependencies, and their graph entries."""
    version = firmware_version(FIRMWARE / "charger-module" / "src" / "blade.h")
    me = {
        "type": "firmware",
        "bom-ref": f"charger-module-firmware@{version}",
        "name": "charger-module-firmware",
        "version": version,
        "supplier": SUPPLIER,
        "licenses": licenses("MIT", None),
        "externalReferences": [{"type": "vcs", "url": PROJECT_URL}],
    }
    deps = fetched_components() + ([c_library] if c_library else [])
    graph = [{"ref": me["bom-ref"], "dependsOn": [d["bom-ref"] for d in deps]}]
    return me, deps, graph


def controller(sdk: pathlib.Path, c_library: dict | None) -> tuple[dict, list, list]:
    version = firmware_version(FIRMWARE / "controller" / "src" / "manifold.h")
    me = {
        "type": "firmware",
        "bom-ref": f"controller-firmware@{version}",
        "name": "controller-firmware",
        "version": version,
        "supplier": SUPPLIER,
        "licenses": licenses("MIT", None),
        "externalReferences": [{"type": "vcs", "url": PROJECT_URL}],
    }

    deps = [github_component("pico-sdk", sdk_version(sdk), "raspberrypi/pico-sdk", "BSD-3-Clause", None)]
    for d, name, repo, spdx, (header, macro), tag in SDK_LIBS:
        v = define(sdk / "lib" / d / header, macro)
        ref = tag.format(v=v, u=v.replace(".", "_"))
        deps.append(github_component(name, v, repo, spdx, NAMED_LICENSES.get(name), ref))

    mono = FIRMWARE / "controller" / "lib" / "monocypher" / "monocypher.h"
    m = re.search(r"Monocypher version (\S+)", mono.read_text())
    if not m:
        sys.exit(f"{mono}: no version line")
    deps.append(github_component("monocypher", m.group(1), "LoupVaillant/Monocypher",
                                 "BSD-2-Clause OR CC0-1.0", None))
    if c_library:
        deps.append(c_library)

    blade, blade_deps, graph = charger_module(c_library)
    deps.append(blade)
    graph.insert(0, {"ref": me["bom-ref"], "dependsOn": [d["bom-ref"] for d in deps]})
    seen = {d["bom-ref"] for d in deps}
    deps += [d for d in blade_deps if d["bom-ref"] not in seen]
    return me, deps, graph


def commit_time() -> str:
    try:
        return subprocess.run(["git", "-C", str(REPO), "log", "-1", "--format=%cI"],
                              capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        sys.exit("sbom: needs the git checkout to date the bill of materials")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("product", choices=["controller", "charger-module"])
    ap.add_argument("--sdk", type=pathlib.Path, help="the pico-sdk the controller was built with")
    ap.add_argument("--out", required=True, type=pathlib.Path)
    args = ap.parse_args()

    c_library = newlib()
    if args.product == "controller":
        if not args.sdk:
            ap.error("controller needs --sdk")
        me, deps, graph = controller(args.sdk.expanduser(), c_library)
    else:
        me, deps, graph = charger_module(c_library)

    # every component appears in the graph, a leaf with no dependencies of its own
    listed = {g["ref"] for g in graph}
    graph += [{"ref": d["bom-ref"], "dependsOn": []} for d in deps if d["bom-ref"] not in listed]

    bom = {
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "serialNumber": f"urn:uuid:{uuid.uuid5(uuid.NAMESPACE_URL, PROJECT_URL + '/' + me['bom-ref'])}",
        "version": 1,
        "metadata": {
            "timestamp": commit_time(),
            "tools": {"components": [{"type": "application", "name": "firmware/tools/sbom.py"}]},
            "component": me,
            "manufacturer": SUPPLIER,
            "supplier": SUPPLIER,
        },
        "components": deps,
        "dependencies": graph,
    }
    args.out.write_text(json.dumps(bom, indent=2) + "\n")


if __name__ == "__main__":
    main()
