# Security policy

## Reporting a vulnerability

Report security problems privately, by either of these routes:

- Email **security@powermanifold.io**.
- Use [Report a vulnerability](https://github.com/mikesmitty/power-manifold/security/advisories/new)
  on this repository's Security tab.

Do not open a public issue for a security problem.

A useful report includes:

- the firmware version (shown on the web page, in `info` on the console, and in Home Assistant)
- what an attacker needs: network access, physical access, a configured token, and so on
- the steps to reproduce the problem, or a proof of concept
- the effect: what the attacker can read, change or stop

## What happens next

1. We acknowledge the report within 5 business days.
2. We confirm the problem and assess its severity, and tell you the result.
3. We prepare a fix and send you status updates at least every 14 days until it is released.
4. We release the fix and publish an advisory. The advisory credits you unless you ask us not to.

We ask that you keep the details private until the fix is released, or for 90 days from your report,
whichever comes first. If a fix needs more time, we will explain why and agree on a new date with you.

A problem found in a third-party component is reported to that component's maintainers as well.

## Scope

- The controller firmware (`firmware/controller`), including its web page, HTTP API, MQTT client,
  Wi-Fi setup over Bluetooth and the update mechanism.
- The charger module firmware (`firmware/charger-module`).
- The update server at `fw.powermanifold.io`.
- The Wi-Fi setup page on the documentation site.

The controller is designed for use on a trusted local network. Its web page, API and MQTT
connection are not intended to be reachable from the internet. Reports that need access to the
local network are in scope. This design affects how severe a problem is rated.
The [Security](https://docs.powermanifold.io/guide/security/) page of the documentation
describes this design for owners.

## Supported versions

Security fixes are released in a new version of the controller firmware. The controller firmware
carries the charger module firmware and installs it on the blades, so a fix to either one reaches
the device through a controller update. Updates are free.

Only the latest release receives fixes. Earlier versions are upgraded by installing the latest
release, as described in [Updates](https://docs.powermanifold.io/guide/updates/).

## Advisories and the software bill of materials

Fixed vulnerabilities are published as
[security advisories](https://github.com/mikesmitty/power-manifold/security/advisories) on this
repository and are named in the release notes of the version that fixes them.

Each firmware release includes a software bill of materials in CycloneDX format
(`controller.cdx.json`, `charger-module.cdx.json`). It lists the third-party components the
firmware is built from and their versions.
