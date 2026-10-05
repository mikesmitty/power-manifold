---
title: HTTP API
description: Read Power Manifold's status, switch ports, change settings and install updates over HTTP, plus the Prometheus metrics endpoint.
sidebar:
  order: 2
---

Everything on the web page is also available as JSON at
`http://<name>.local/api/v1/`. Reading status needs no password. Anything
that changes something needs your API token. A new controller has none and
refuses every change until one is set;
[first-time setup](/power-manifold/setup/first-time-setup/) explains how the
first one gets in.

```sh
curl -H "Authorization: Bearer $TOKEN" ...
```

## Reading

| Request | Returns |
| --- | --- |
| `GET /api/v1/status` | Everything the web page shows. See below. |
| `GET /api/v1/faults` | The fault log, newest first, eight entries per page. Add `?offset=8` for the next page. |
| `GET /api/v1/log` | The controller's recent messages as text. Needs the token. |
| `GET /api/v1/settings` | Every setting except passwords. Needs the token. |
| `GET /api/v1/settings/export` | Every setting, ready to import later. Add `?secrets=1` to include passwords. Needs the token. |
| `GET /metrics` | [Prometheus metrics](#prometheus). |

**Status, per port:** `name`, `state`, `v`, `i`, `p`, `e`, `pdo`,
`contract_w`, `prio`, `limit_ma`, `max_v`, `boot`, `attached`, `charged`,
`fault`, `t_conv`, `t_plug`, `t_mcu`, `progress`, `update_due`, `silent`.

**Status, chassis:** `total_w`, `reserved_w`, `budget_w`, `headroom_w`,
`energy_kwh`, `fan`, `fan_mode`, `alert`, `rssi`, `eth`, `ble`, `uptime_s`,
`fw`, `blade_fw`, `slot`, `trial`, `boot`, `warm_start`, `vin_v`,
`ceiling_ma`, `problem`, `problems`, `led_mode`, `led_now`, and a `ups`
object.

## Changing things

All of these need the token.

| Request | Body |
| --- | --- |
| `POST /api/v1/port/<n>` | `{"action": "enable"}`, `"disable"`, `"hard_reset"`, `"src_cap"` (re-announce) or `"update"` (rewrite the blade's firmware now) |
| `POST /api/v1/budget` | `{"watts": 240}` |
| `POST /api/v1/fan` | `{"on": true}`, `{"on": false}` or `{"mode": "auto"}` |
| `POST /api/v1/settings` | any of the settings keys below; the reply says whether a reboot is needed |
| `POST /api/v1/faults/clear` | none; empties the fault log |
| `POST /api/v1/reboot` | none; restarts the controller without cutting port power |
| `POST /api/v1/update` | a signed firmware file, see below |

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
| Network | `ip_mode`, `ip`, `netmask`, `gateway`, `dns`, `ntp_server`, `syslog_host`, `syslog_port`, `update_url` |
| Power | `budget_w`, `fan_mode`, `fan_on_w`, `fan_off_w`, `fan_on_ma`, `charged_mw`, `charged_min` |
| Lights | `led_brightness`, `led_boot`, `led_dim`, `led_night`, `led_idle_min`, `tz_offset_min` |
| Ports (arrays of six) | `port_names`, `port_limits_ma`, `port_max_v`, `port_priorities`, `port_boot`, `port_auto_off`, `port_sleep_min` |
| Input voltage | `vin_cal` |

The name, Wi-Fi, MQTT broker and addressing apply after
`POST /api/v1/reboot`. Everything else applies at once.

Example: give port 1 priority 0 and leave the others as they are.

```sh
curl -X POST -H "Authorization: Bearer $TOKEN" \
  -d '{"port_priorities":[0,2,3,4,5,6]}' http://pwrman.local/api/v1/settings
```

### Installing an update

Download `controller.signed.bin` from a
[release](https://github.com/mikesmitty/power-manifold/releases) and post
it:

```sh
curl -H "Authorization: Bearer $TOKEN" --data-binary @controller.signed.bin \
  http://pwrman.local/api/v1/update
```

The controller checks the signature, installs it and restarts on trial,
exactly as described in [Updates](/power-manifold/guide/updates/). It
refuses unsigned files, files for other hardware and older versions.

## Prometheus

`GET /metrics` serves Prometheus text format with no login. It includes the
chassis totals, the input voltage, UPS readings when one is connected, and
every port metric labelled with `port` and `name`. Blade temperatures are
`pwrman_port_temperature_celsius` with a `sensor` label (`converter`,
`plug`, `mcu`).

```yaml
scrape_configs:
  - job_name: power-manifold
    static_configs:
      - targets: ["pwrman.local"]
```
