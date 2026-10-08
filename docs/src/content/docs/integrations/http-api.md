---
title: HTTP API
description: Read Power Manifold's status, switch ports, change settings and install updates over HTTP, plus the Prometheus metrics endpoint.
sidebar:
  order: 2
---

Everything in the web interface is also available as JSON at
`http://<name>.local/api/v1/`. Reading status needs no password. Anything
that changes something needs your API token. A new controller has no token and
refuses every change until one is set.
[First-time setup](/setup/first-time-setup/) describes how to
set the first token.

With [HTTPS](/integrations/https/) on, use `https://` and the name on the
certificate. Port 80 then answers every request with a redirect.

## Examples

Set these once in your shell, then copy any example below:

```sh
PM=http://pwrman.local
TOKEN=your-api-token
```

<details>
<summary>Read the status</summary>

```sh
curl $PM/api/v1/status
```

```jsonc
{
  "name": "pwrman", "fw": "0.12.0", "slot": "A",
  "trial": false, "uptime_s": 274620, "rssi": -58,
  "eth": "100M full, 192.168.1.40",
  "total_w": 110.42, "reserved_w": 202.0,
  "budget_w": 240.0, "headroom_w": 38.0,
  "energy_kwh": 17.850, "fan": "on", "fan_mode": "auto",
  "alert": false, "improv": "off", "boot": "power-on",
  "warm_start": false, "vin_v": 24.06, "ceiling_ma": 0,
  "problem": false, "problems": "",
  "led_mode": "normal", "led_now": 48,
  "chassis_light": "ok", "blade_fw": "0.2.0",
  "ups": {"present": false},
  "ports": [
    {"name": "Laptop", "state": "active", "gen": 3,
     "attached": true, "charged": false, "pdo": 5,
     "v": 20.021, "i": 2.712, "p": 54.30, "e": 6.211,
     "contract_w": 65.0, "prio": 2, "limit_ma": 5000,
     "max_v": 20, "boot": "on", "fault": 0,
     "t_conv": 54.9, "t_plug": 41.3, "t_mcu": 36.2,
     "progress": 0, "update_due": false, "fw": "0.2.0",
     "silent": false}
    // ...one entry per port, six in all
  ]
}
```

</details>

<details>
<summary>Read the fault log</summary>

```sh
curl $PM/api/v1/faults
```

```json
{"available": true, "count": 4, "offset": 0, "faults": [
  {"seq": 41, "epoch": 1791255690, "uptime_s": 262840, "port": 4,
   "type": "fault", "code": 256, "arg": 0, "power_w": 26.4,
   "contract_w": 27.0, "text": "ocp"}
]}
```

`epoch` is 0 for entries logged before the controller knew the time.

</details>

<details>
<summary>Switch a port off, and on again</summary>

```sh
curl -X POST -H "Authorization: Bearer $TOKEN" \
  -d '{"action":"disable"}' $PM/api/v1/port/3
curl -X POST -H "Authorization: Bearer $TOKEN" \
  -d '{"action":"enable"}' $PM/api/v1/port/3
```

```json
{"ok": true}
```

</details>

<details>
<summary>Set the power budget</summary>

```sh
curl -X POST -H "Authorization: Bearer $TOKEN" \
  -d '{"watts":240}' $PM/api/v1/budget
```

```json
{"ok": true}
```

Outside 15–600 W the reply is `400` with `{"error": "watts out of range"}`.

</details>

<details>
<summary>Make port 3 the most important</summary>

Port arrays are read in port order, so send all six values. By default
port 1 has priority 0, port 2 has 1 and so on.

```sh
curl -X POST -H "Authorization: Bearer $TOKEN" \
  -d '{"port_priorities":[1,2,0,3,4,5]}' $PM/api/v1/settings
```

```json
{"ok": true, "reboot_required": false}
```

</details>

<details>
<summary>Back up and restore every setting</summary>

```sh
# Back up, passwords included. Keep this file private.
curl -H "Authorization: Bearer $TOKEN" \
  "$PM/api/v1/settings/export?secrets=1" -o pwrman-settings.json

# Restore it, for example after a factory reset, then restart to apply
# the network settings.
curl -X POST -H "Authorization: Bearer $TOKEN" \
  --data-binary @pwrman-settings.json $PM/api/v1/settings
curl -X POST -H "Authorization: Bearer $TOKEN" $PM/api/v1/reboot
```

After a factory reset the controller has no token. Complete
[first-time setup](/setup/first-time-setup/) to set one, then
restore the file with the commands above or with **Import…** in the web
interface's **Settings**.

</details>

<details>
<summary>Install an update</summary>

```sh
curl -H "Authorization: Bearer $TOKEN" \
  --data-binary @controller.signed.bin $PM/api/v1/update
```

```json
{"ok": true, "slot": "B", "bytes": 854580, "version": "0.13.0",
 "action": "trial reboot in 1s"}
```

</details>

<details>
<summary>What a refused request looks like</summary>

A change without the token gets `401`:

```json
{"error": "bearer token required"}
```

On a controller with no token yet, the reply is
`{"error": "no API token set yet: finish first-time setup first"}`.
A request whose `Host` header is not the controller's IP address, its
device name, the device name with `.local`, or one of its
[host names](/integrations/network/#host-names) gets `403`. During
first-time setup, a settings change must be sent with
`Content-Type: application/json`, or it gets `415`.
A bad value gets `400` with the reason, such as
`{"error": "port_limits_ma: 500-5000 mA each"}`.

</details>

## Reading

| Request | Returns |
| --- | --- |
| `GET /api/v1/status` | Everything the web interface shows. See below. |
| `GET /api/v1/faults` | The fault log, newest first, eight entries per page. Add `?offset=8` for the next page. |
| `GET /api/v1/log` | The controller's recent messages as text. Needs the token. |
| `GET /api/v1/settings` | Every setting except passwords. Needs the token. |
| `GET /api/v1/settings/export` | Every setting, ready to import later. Add `?secrets=1` to include passwords. Needs the token. |
| `GET /api/v1/tls` | The HTTPS certificate: `enabled`, `active`, `installed`, then `names`, `issuer`, `expires`, `days_left` and `sha256` when one is installed. |
| `GET /metrics` | [Prometheus metrics](#prometheus). |

**Status, per port:** `name`, `state`, `gen` (blade generation), `v`, `i`, `p`, `e`, `pdo`,
`contract_w`, `prio`, `limit_ma`, `max_v`, `boot`, `attached`, `charged`,
`fault`, `t_conv`, `t_plug`, `t_mcu`, `progress`, `update_due` (a
[blade update](/guide/updates/#blade-firmware) is waiting), `fw` (the
blade's firmware version, `null` when unknown), `silent`.

**Status, chassis:** `name`, `total_w`, `reserved_w`, `budget_w`, `headroom_w`,
`energy_kwh`, `fan`, `fan_mode`, `alert`, `rssi`, `eth`, `improv`, `uptime_s`,
`fw`, `blade_fw`, `slot`, `trial`, `boot`, `warm_start`, `vin_v`,
`ceiling_ma`, `problem`, `problems`, `led_mode`, `led_now`,
`chassis_light`, and the `update`, `net` and `ups` objects.

**Status, `update`:** `available` (a newer release is known), `latest`
(its version, or `null`), `check` (`off`, `never`, `checking`, `ok` or
`failed`), `check_age_s` (seconds since the last check finished),
`installing` and `progress` (percent of the download), `restarting`
(the new image is installed and the controller restarts into it within a
second), and what
[automatic updates](/guide/updates/#automatic-updates) will do: `auto`
(`off`; `none`, no newer release; `waiting`, for `auto_wait_s` more
seconds; `scheduled`, for 03:00 local time; `retry`, the last attempt
failed and the next night tries again; `installing`; `postponed`, until
the unix time `postponed_until`; or `skipped`) and `skipped` (the version
that does not install automatically, or `null`).

**Status, `net`:** what the controller is using now, which can differ
from the settings: `up`, `ip`, `netmask`, `gateway`, `dns`, `ntp`,
`time_synced`, `mqtt` (`off`, `connecting`, `waiting_for_clock` or
`connected`) and `mqtt_problem` (why a TLS link to the broker cannot
connect, such as a broker certificate that does not carry the broker's
name, or `null`). Addresses are `null` while the network is down.

`chassis_light` is what the chassis light shows: `bus_fault` (red, fast
blink: input voltage out of range), `wifi_setup` (blue: Wi-Fi setup
open), `setup` (amber: first-time setup open) or `ok` (dim green).

## Commands

All of these need the token.

| Request | Body |
| --- | --- |
| `POST /api/v1/port/<n>` | `{"action": "enable"}`, `"disable"`, `"hard_reset"`, `"src_cap"` (re-announce) or `"update"` (rewrite the blade's firmware now) |
| `POST /api/v1/budget` | `{"watts": 240}` |
| `POST /api/v1/fan` | `{"on": true}`, `{"on": false}` or `{"mode": "auto"}` |
| `POST /api/v1/settings` | any of the settings keys below; the reply says whether a reboot is needed |
| `POST /api/v1/faults/clear` | none; empties the fault log |
| `POST /api/v1/reboot` | none; restarts the controller without cutting port power |
| `POST /api/v1/update/check` | none; asks the update source for the newest release now. The answer appears in the status `update` object. |
| `POST /api/v1/update/latest` | none; downloads and installs the newest known release, see [Installing an update](#installing-an-update) |
| `POST /api/v1/update/postpone` | none; no automatic install for the next 7 days. `409` until the controller has the time. |
| `POST /api/v1/update/skip` | none; the newest known release does not install automatically. `409` when no newer release is known. |
| `POST /api/v1/improv` | `{"open": true}` opens Wi-Fi setup for 10 minutes; `{"open": false}` closes it |
| `POST /api/v1/update` | a signed firmware file, see below |
| `POST /api/v1/tls` | the private key and certificate chain as PEM text; replies like `GET /api/v1/tls`. See [HTTPS](/integrations/https/). |
| `POST /api/v1/tls/remove` | none; removes the certificate. `409` while HTTPS is on. |

Example: switch port 3 off.

```sh
curl -X POST -H "Authorization: Bearer $TOKEN" \
  -d '{"action":"disable"}' http://pwrman.local/api/v1/port/3
```

### Settings keys

`POST /api/v1/settings` accepts any subset of these. The export file uses
the same keys, so you can edit an export and post it back.

| Area | Keys |
| --- | --- |
| Device | `name`, `token` |
| Wi-Fi | `wifi_ssid`, `wifi_pass` |
| MQTT | `mqtt_host`, `mqtt_port`, `mqtt_user`, `mqtt_pass`, `mqtt_tls`, `mqtt_tls_verify`, `mqtt_ca` (PEM) |
| Network | `ip_mode`, `ip`, `netmask`, `gateway`, `dns`, `ntp_server`, `hostnames`, `syslog_host`, `syslog_port`, `update_url`, `https` |
| Updates | `update_auto` (`true`: new releases install by themselves) |
| Power | `budget_w`, `fan_mode`, `fan_on_w`, `fan_off_w`, `fan_on_ma`, `charged_mw`, `charged_min` |
| Lights | `led_brightness`, `led_boot`, `led_dim`, `led_night`, `led_idle_min`, `tz_offset_min` |
| Ports (arrays of six) | `port_names`, `port_limits_ma`, `port_max_v`, `port_priorities`, `port_boot`, `port_auto_off`, `port_sleep_min` |

`vin_cal`, the input voltage trim, appears in `GET /api/v1/settings` but
not in exports, and posting it changes nothing. It belongs to the unit's
own measuring circuit and can't be changed over the API, because the
[3 A cap on a sagging input](/guide/troubleshooting/#ports-capped-at-3-a)
acts on the trimmed reading.

`https` also appears in `GET /api/v1/settings` but not in exports, because
the certificate it needs stays on the controller. Setting it to `true`
returns `400` until a certificate is installed.

The name, Wi-Fi, MQTT broker and addressing apply after
`POST /api/v1/reboot`. Everything else applies at once.

Port arrays are read in port order. An array shorter than six changes only
the first ports, so to change a later port, send all six values. See
[Make port 3 the most important](#examples).

### Installing an update

To install the newest release the controller knows of, let it download it:

```sh
curl -X POST -H "Authorization: Bearer $TOKEN" http://pwrman.local/api/v1/update/latest
```

`409` means no release is known yet: send `POST /api/v1/update/check`
first, then wait for `update.latest` in the status. While it downloads,
`update.installing` is `true` and `update.progress` counts up. Then
`update.restarting` is `true` until the controller restarts into the new
firmware.

Or download `controller.signed.bin` from a
[release](https://github.com/mikesmitty/power-manifold/releases) and post
it:

```sh
curl -H "Authorization: Bearer $TOKEN" --data-binary @controller.signed.bin \
  http://pwrman.local/api/v1/update
```

Either way the controller checks the signature, installs it and restarts on
trial, exactly as described in [Updates](/guide/updates/). It
refuses unsigned files, files for other hardware and older versions.

## The web console

The web interface's Console tab sends console commands to
`POST /api/v1/console` and reads the reply from `GET /api/v1/console`. The
reply is text for people to read, and its wording can change in any
release. Programs should use the JSON requests above. The commands are
listed in the [console reference](/developers/controller/console/).

## Prometheus

`GET /metrics` serves Prometheus text format with no login. It includes the
chassis totals, the input voltage, UPS readings when one is connected, and
every port metric labeled with `port` and `name`. Blade temperatures are
`pwrman_port_temperature_celsius` with a `sensor` label (`converter`,
`plug`, `mcu`). `pwrman_http_refused_total` counts the requests refused
since the controller started (see [What a refused request looks
like](#examples)). With a certificate installed,
`pwrman_https_certificate_expiry_seconds` is when it expires, as a Unix
time.

```yaml
scrape_configs:
  - job_name: power-manifold
    static_configs:
      - targets: ["pwrman.local"]
```
