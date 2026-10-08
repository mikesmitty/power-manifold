#!/usr/bin/env python3
"""Check docs/openapi.yaml against the code that serves the API.

Fails when the two disagree on any of these:
  * the routes: every "GET /..." or "POST /..." that src/net/http.c answers
    is a path and method in the description, and the reverse;
  * the reply objects built field by field (the status and its ups, update
    and net blocks, a port, the fault log and one fault, the certificate
    details): the keys in each builder are exactly the properties of its
    schema, in both directions;
  * the fixed words in them (port states, update states, the chassis light
    and the rest in WORDS below) and the port actions: exactly the enum;
  * the settings: the keys the settings reply and the export with secrets
    actually carry (printed by the settings_json_dump host program) are
    exactly the properties of Settings and SettingsExport, and the keys
    settings_json_apply reads are exactly those of SettingsChange;
  * everything else in http.c: each reply key, each key read from a request
    body and each query parameter appears somewhere in the description;
  * the description's version is the firmware's FW_VERSION.

What it cannot see: which status codes a route returns, descriptions and
value ranges. Those stay a review job.

Python has no YAML reader in its standard library, so read_yaml() below
reads the subset the description is written in: block mappings and
sequences, plain and quoted scalars, one-line [flow, lists] and | blocks.
Example values are skipped unread. The docs site's build validates the
whole file against the OpenAPI 3.1 schema, examples included.

  check_openapi.py --controller firmware/controller --settings-dump <exe>
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys

# ---- the YAML subset ---------------------------------------------------------

KEY = re.compile(r"""^('(?:[^']|'')*'|"[^"]*"|[^'"{\[\s#][^:]*?):(?:\s+(.*))?$""")


def unquote(s):
    if len(s) >= 2 and s[0] == s[-1] == "'":
        return s[1:-1].replace("''", "'")
    if len(s) >= 2 and s[0] == s[-1] == '"':
        return s[1:-1]
    return s


def scalar(text):
    text = text.strip()
    if text.startswith("'"):
        end = text.find("'", 1)
        while end != -1 and text[end + 1 : end + 2] == "'":
            end = text.find("'", end + 2)
        return unquote(text[: end + 1])
    if text.startswith('"'):
        return unquote(text[: text.find('"', 1) + 1])
    if text.startswith("["):
        inner = text[1 : text.rindex("]")]
        return [scalar(p) for p in inner.split(",") if p.strip()]
    if text.startswith("{"):
        return None  # only examples use flow mappings
    cut = text.find(" #")
    return text[:cut].rstrip() if cut != -1 else text


class Reader:
    def __init__(self, path):
        self.lines = []
        for raw in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
            text = raw.strip()
            if text and not text.startswith("#"):
                self.lines.append((len(raw) - len(raw.lstrip(" ")), text))
        self.i = 0

    def fail(self, why):
        ind, text = self.lines[min(self.i, len(self.lines) - 1)]
        raise SystemExit(f"check_openapi: cannot read the description near {text!r}: {why}")

    def skip_deeper(self, indent):
        while self.i < len(self.lines) and self.lines[self.i][0] > indent:
            self.i += 1

    def node(self, indent):
        return self.seq(indent) if self.lines[self.i][1].startswith("-") else self.map(indent)

    def map(self, indent):
        out = {}
        while self.i < len(self.lines):
            ind, text = self.lines[self.i]
            if ind < indent:
                break
            if ind > indent or text.startswith("-"):
                self.fail("unexpected indentation")
            m = KEY.match(text)
            if not m:
                self.fail("expected 'key: value'")
            key, rest = unquote(m.group(1)), (m.group(2) or "").strip()
            self.i += 1
            out[key] = self.value(indent, key, rest)
        return out

    def value(self, indent, key, rest):
        if key in ("example", "examples"):
            self.skip_deeper(indent)
            return None
        if rest in ("|", "|-", ">", ">-"):
            self.skip_deeper(indent)
            return ""
        if rest:
            return scalar(rest)
        if self.i < len(self.lines):
            ind, text = self.lines[self.i]
            if ind > indent or (ind == indent and text.startswith("- ")):
                return self.node(ind)
        return None

    def seq(self, indent):
        out = []
        while self.i < len(self.lines):
            ind, text = self.lines[self.i]
            if ind != indent or not text.startswith("-"):
                break
            item = text[1:].lstrip()
            if KEY.match(item):  # "- key: value" opens a mapping
                inner = indent + len(text) - len(item)
                self.lines[self.i] = (inner, item)
                out.append(self.map(inner))
            else:
                self.i += 1
                out.append(scalar(item))
        return out


def read_yaml(path):
    r = Reader(path)
    doc = r.map(0)
    if r.i != len(r.lines):
        r.fail("unexpected indentation")
    return doc


# ---- the description ---------------------------------------------------------

METHODS = {"get", "put", "post", "delete", "patch", "head", "options"}


def walk(node):
    yield node
    if isinstance(node, dict):
        for v in node.values():
            yield from walk(v)
    elif isinstance(node, list):
        for v in node:
            yield from walk(v)


class Spec:
    def __init__(self, path):
        self.doc = read_yaml(path)
        self.schemas = self.doc["components"]["schemas"]

    def routes(self):
        out = set()
        for path, item in self.doc["paths"].items():
            for method in item:
                if method in METHODS:
                    out.add(f"{method.upper()} {re.sub(r'{[^}]*}', '{}', path)}")
        return out

    def props(self, schema):
        if "$ref" in schema:
            return self.props(self.schemas[schema["$ref"].rsplit("/", 1)[1]])
        keys = set(schema.get("properties") or {})
        for part in schema.get("allOf") or []:
            keys |= self.props(part)
        return keys

    def schema_props(self, name):
        if name not in self.schemas:
            raise SystemExit(f"check_openapi: no schema {name} in the description")
        return self.props(self.schemas[name])

    def prop_schema(self, schema, prop):
        if "$ref" in schema:
            return self.prop_schema(self.schemas[schema["$ref"].rsplit("/", 1)[1]], prop)
        found = (schema.get("properties") or {}).get(prop)
        for part in schema.get("allOf") or []:
            found = found or self.prop_schema(part, prop)
        if found and "$ref" in found:
            found = self.schemas[found["$ref"].rsplit("/", 1)[1]]
        return found

    def prop_enum(self, schema, prop):
        found = self.prop_schema(self.schemas[schema], prop)
        return {str(v) for v in (found or {}).get("enum") or []}

    def names(self):  # every property name anywhere
        out = set()
        for n in walk(self.doc):
            if isinstance(n, dict) and isinstance(n.get("properties"), dict):
                out |= set(n["properties"])
        return out

    def values(self):  # every enum value and const anywhere
        out = set()
        for n in walk(self.doc):
            if isinstance(n, dict):
                if isinstance(n.get("enum"), list):
                    out |= {str(v) for v in n["enum"]}
                if isinstance(n.get("const"), str):
                    out.add(n["const"])
        return out

    def query_params(self):
        out = set()
        for n in walk(self.doc["paths"]):
            if isinstance(n, dict) and n.get("in") == "query":
                out.add(n["name"])
        return out


# ---- the code ----------------------------------------------------------------

# "GET /path" or "POST /path" at the start of a string literal. A literal
# ending in "/" is a prefix, not a route: "GET /" serves the web page and
# "POST /api/v1/" is where the token check for every change sits.
ROUTE = re.compile(r'"(GET|POST) (/[A-Za-z0-9_./%-]*)')
JSON_KEY = re.compile(r'\\"([a-z0-9_]+)\\":')

# Builders whose keys must match a schema exactly. The keys up to and
# including `head_end` belong to the first schema, the rest to the second:
# the status object's own fields end at "ports" and each port's follow, and
# a block such as "ups":{...} names its parent's key first. A repeat of
# head_end in the tail is that same parent key, built in another branch.
BUILDERS = [
    ("src/net/http.c", "build_status_json", "Status", "ports", "Port"),
    ("src/net/http.c", "build_ups_json", "Status", "ups", "Ups"),
    ("src/net/http.c", "build_update_json", "Status", "update", "UpdateStatus"),
    ("src/net/http.c", "build_net_json", "Status", "net", "NetStatus"),
    ("src/net/http.c", "build_faults_json", "FaultLog", "faults", "Fault"),
    ("src/net/https.c", "https_json", "Tls", None, None),
]


# Fixed words the firmware sends, and the enum each must match exactly: the
# string literals in the named function, or in the one statement in it that
# starts with `statement`.
WORDS = [
    ("src/engine/port_fsm.c", "port_state_name", None, "Port", "state"),
    ("src/settings_util.c", "settings_port_boot_name", None, "Port", "boot"),
    ("src/net/improv.c", "improv_state_str", None, "Status", "improv"),
    ("src/led_sched.c", "led_mode_name", None, "Status", "led_mode"),
    ("src/flash_map.c", "flash_map_slot_name", None, "Status", "slot"),
    ("src/net/http.c", "chassis_light_name", None, "Status", "chassis_light"),
    ("src/net/update_check.c", "update_check_state", None, "UpdateStatus", "check"),
    ("src/update_auto.c", "update_auto_name", None, "UpdateStatus", "auto"),
    ("src/net/http.c", "build_net_json", "const char *mqtt =", "NetStatus", "mqtt"),
    ("src/net/http.c", "build_faults_json", "const char *type =", "Fault", "type"),
]
WORD = re.compile(r'"([A-Za-z0-9_-]+)"')
BODY_WORD = re.compile(r'strstr\(body, "\\"([a-z0-9_]+)\\""\)')


def function_body(src, name, where):
    # a definition at the start of a line, its parameters on one line or more
    m = re.search(r"^[A-Za-z][^;{}]*?\b" + name + r"\([^;{}]*\)\s*\{\s*$", src, re.M)
    if not m:
        raise SystemExit(f"check_openapi: no function {name} in {where}")
    return src[m.end() : src.index("\n}\n", m.end())]


def compare(problems, what, code, spec, code_name):
    for k in sorted(code - spec):
        problems.append(f"{what}: {k!r} is in {code_name} but not in the description")
    for k in sorted(spec - code):
        problems.append(f"{what}: {k!r} is in the description but not in {code_name}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--controller", required=True, type=pathlib.Path,
                    help="firmware/controller")
    ap.add_argument("--settings-dump", required=True,
                    help="the settings_json_dump host program")
    args = ap.parse_args()
    root = args.controller
    spec = Spec(root / "docs/openapi.yaml")
    http_c = (root / "src/net/http.c").read_text(encoding="utf-8")
    problems = []

    # the routes
    code_routes = set()
    for method, path in ROUTE.findall(http_c):
        if not path.endswith("/"):
            code_routes.add(f"{method} {re.sub(r'%[a-z]+', '{}', path)}")
    compare(problems, "routes", code_routes, spec.routes(), "src/net/http.c")

    # the objects built field by field
    found = {}
    for file, func, head, head_end, tail in BUILDERS:
        keys = JSON_KEY.findall(function_body((root / file).read_text(encoding="utf-8"), func, file))
        cut = keys.index(head_end) + 1 if head_end else len(keys)
        found.setdefault(head, (set(), set()))[0].update(keys[:cut])
        found[head][1].add(f"{file} {func}")
        if tail:
            found.setdefault(tail, (set(), set()))[0].update(k for k in keys[cut:] if k != head_end)
            found[tail][1].add(f"{file} {func}")
    for schema, (keys, funcs) in found.items():
        compare(problems, schema, keys, spec.schema_props(schema), ", ".join(sorted(funcs)))

    # the fixed words in those objects
    for file, func, statement, schema, prop in WORDS:
        text = function_body((root / file).read_text(encoding="utf-8"), func, file)
        if statement:
            start = text.index(statement)
            text = text[start : text.index(";", start)]
        compare(problems, f"{schema}.{prop}", set(WORD.findall(text)),
                spec.prop_enum(schema, prop), f"{file} {func}")

    # the port actions
    start = http_c.index('"POST /api/v1/port/%u"')
    actions = set(BODY_WORD.findall(http_c[start : http_c.index("ipc_cmd_push", start)]))
    body = spec.doc["paths"]["/api/v1/port/{port}"]["post"]["requestBody"]
    action = body["content"]["application/json"]["schema"]["properties"]["action"]
    compare(problems, "port actions", actions, {str(v) for v in action["enum"]}, "src/net/http.c")

    # the settings, as built
    out = subprocess.run([args.settings_dump], capture_output=True, text=True, check=True).stdout
    built = dict(line.split(" ", 1) for line in out.splitlines())
    compare(problems, "Settings", set(json.loads(built["settings"])),
            spec.schema_props("Settings"), "GET /api/v1/settings")
    compare(problems, "SettingsExport", set(json.loads(built["export"])),
            spec.schema_props("SettingsExport"), "the export with secrets")

    # the settings, as read
    apply = function_body((root / "src/settings_json.c").read_text(encoding="utf-8"),
                          "settings_json_apply", "src/settings_json.c")
    read = set(re.findall(r'(?:take_str|json_get_\w+)\(body, "([a-z0-9_]+)"', apply))
    read |= set(re.findall(r'\{"([a-z0-9_]+)", offsetof', apply))
    compare(problems, "SettingsChange", read, spec.schema_props("SettingsChange"),
            "settings_json_apply")

    # everything else the web server reads or writes
    names, values = spec.names(), spec.values()
    for k in sorted(set(JSON_KEY.findall(http_c)) - names):
        problems.append(f"reply key {k!r} in src/net/http.c is not in the description")
    for k in sorted(set(re.findall(r'json_get_\w+\(body, "([a-z0-9_]+)"', http_c)) - names):
        problems.append(f"request key {k!r} in src/net/http.c is not in the description")
    for k in sorted(set(re.findall(r'strstr\(body, "\\"([a-z0-9_]+)\\""\)', http_c)) - names - values):
        problems.append(f"request word {k!r} in src/net/http.c is not in the description")
    for k in sorted(set(re.findall(r'"\?([a-z0-9_]+)=', http_c)) - spec.query_params()):
        problems.append(f"query parameter {k!r} in src/net/http.c is not in the description")

    # the version
    fw = re.search(r'#define FW_VERSION\s+"([^"]+)"',
                   (root / "src/manifold.h").read_text(encoding="utf-8")).group(1)
    if spec.doc["info"]["version"] != fw:
        problems.append(f"info.version is {spec.doc['info']['version']}, the firmware is {fw}")

    for p in problems:
        print(f"docs/openapi.yaml: {p}")
    if problems:
        print(f"check_openapi: {len(problems)} differences; update docs/openapi.yaml with the code")
        return 1
    print(f"check_openapi: {len(code_routes)} routes and every field match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
