# Serial console

The controller's maintenance console: every setting and action the web page,
the API and MQTT offer, plus bench tools for the backplane bus.

## Reaching it

USB CDC at 115200 from any serial terminal, or RTT through a debug probe.
The controller card's USB connection is an unpopulated header, intended for
bench and development use only. Owners use the
[web interface](https://docs.powermanifold.io/guide/web-interface/) and
[Home Assistant](https://docs.powermanifold.io/guide/home-assistant/).

A console-only first setup looks like this:

```
wifi <ssid> <password>
mqtt <broker-host> [port user pass]
name pwrman
token <t>
save
reboot
```

## Commands

`help` prints the list; on a fake-blade build `sim` adds the fault injection
commands described in [Building](building.md#fake-blade-mode-no-backplane-needed).

| Command | Purpose |
| --- | --- |
| `status` | port table (state — `upd NN%` while a blade is being written — contract, draw, current limit, voltage `cap`, priority, `boot` policy, `chg` once charged, a gen-3 blade's `conv` and `plug` temperatures) and chassis power |
| `info` | firmware, slot and boot reason, links, addressing and time server, UPS supply, bus voltage, broker, syslog sink, LED schedule and local time, problems, simulator state |
| `wifi <ssid> [pass]` | WiFi credentials |
| `improv [on\|off]` | Wi-Fi setup window |
| `mqtt <host> [port user pass]` | broker; an empty host disables MQTT |
| `mqtt tls on\|off\|unverified` | TLS to the broker, verified against the installed certificate or the built-in Let's Encrypt roots |
| `mqtt ca` / `mqtt ca clear` | the installed broker certificate (install one from the page or the API) |
| `ip dhcp` / `ip static <addr> <mask> <gw>` | addressing: the wired link if a W6100 is present, else WiFi |
| `dns <addr>\|auto` | resolver override |
| `ntp <host>\|auto` | time server override |
| `hostnames <name>...\|clear` | more names the web server answers to; its IP address and `<name>.local` always work |
| `https` / `https on\|off` | the HTTPS state and the installed certificate; `on` needs a certificate (installed with `POST /api/v1/tls` or from the page) and makes port 80 redirect |
| `https remove` | remove the certificate; refused while HTTPS is on |
| `syslog <host> [port]` / `syslog off` | mirror the console to a UDP syslog host |
| `name <device-name>` | hostname and topic id |
| `token <t>\|clear` | API bearer token; with none stored the network API refuses every change until one is set here, through Improv, or in the first hour on Ethernet |
| `budget <watts>` | chassis power budget |
| `port <n> on\|off\|reset\|srccap\|update` | port control; `update` writes the bundled gen-3 blade firmware over whatever the blade runs |
| `port <n> priority <0-255>` | 0 = highest; sheds from the bottom |
| `port <n> name <text>\|clear` | label for the web UI and Home Assistant |
| `port <n> limit <500-5000>` | advertised current ceiling in mA, every PDO |
| `port <n> volt 5\|9\|12\|15\|20` | voltage cap: the highest PDO advertised (20 = the whole table) |
| `port <n> boot on\|off\|last` | state at power-up |
| `port <n> autooff on\|off` | switch off once the sink is charged |
| `port <n> sleep <min>\|off` | switch off this long after a sink attaches |
| `charged <mW> <minutes>` | charge-complete thresholds; 0 mW switches detection off |
| `blades [bootopt on\|off \| watch <s>\|off]` | the bundled gen-3 blade firmware and the update policy (see [Blade firmware updates](flash-and-updates.md#blade-firmware-updates)) |
| `fan on\|off\|auto [on_w off_w [on_ma]]` | fan policy |
| `led <0-255>`, `led boot white\|rainbow` | LED brightness and power-up sweep |
| `led dim <0-255>`, `led night <HH:MM> <HH:MM>\|off`, `led idle <minutes>\|off` | dimmed level, night window, idle dimming |
| `tz <+HH:MM\|-HH:MM>` | local time offset for the night window |
| `faults [clear]` | persistent fault log |
| `ups [buzzer on\|off]` | UPS supply readings, status bits, per-block voltages and link counters; `buzzer off` silences its alarm until the supply restarts |
| `vin [cal <volts>\|cal reset]` | DC bus voltage with the raw count and gain trim; `cal 24.13` trims the reading to a meter's (then `save`) |
| `button [short\|long]` | front-panel button input and state; `short` (wake the chain) / `long` (open Wi-Fi setup) act as if it had been pressed |
| `export` | every setting as JSON, without passwords |
| `update [--unsigned] [--downgrade] <http-url>\|latest` | OTA pull into the inactive slot, `latest` being the newest release the controller knows of; the flags let in an unsigned or an older image, which only this console can do |
| `update check`, `update source <http-url>\|default\|off` | ask the update source for the newest release now; set where the daily check asks, or stop it asking (then `save`) |
| `update auto on\|off\|postpone\|skip` | turn automatic installs on or off (then `save`), put them off for 7 days, or skip the newest known release |
| `stack` | per-core stack high-water marks |
| `i2c scan <ch\|none>`, `i2c read <ch> <addr> <reg> [n]`, `i2c write <ch> <addr> <reg> <val>`, `i2c en <port> on\|off` | bench access to the backplane bus, run on the engine core: scan a mux channel (`none` = the upstream side), read or write a register, drive a blade's EN; a healthy blade segment answers `0x40 0x61 0x70 0x74` |
| `save`, `defaults`, `reboot`, `bootsel` | settings and lifecycle |

Each command goes into the log ring, and on to a syslog host, as one line:
`console> ` and the command, with the password of `wifi` and `mqtt` and the
value of `token` shown as `********`. The mask is always eight characters,
whatever the secret's length. The keys as they are typed never reach the log.

## From the web page

The page's **Console** tab, and `POST /api/v1/console`, run the same
commands behind the API token. A batch of up to 2 KB, one command per
line, runs in order from the main loop; blank lines and lines starting
with `#` are skipped. Each line is logged and echoed as `web> ` and the
command, masked as above, followed by what it printed. The reply holds up
to 8 KB.

These are refused there and work only on the serial console:

| Refused on the web | Why |
| --- | --- |
| `token` | the web console already runs behind the token |
| `update --unsigned`, `update --downgrade` | the update signing and no-downgrade rules hold for everything that arrives over the network |
| `defaults` | it clears the token along with every other setting |
| `bootsel` | it stops the firmware until someone reaches the box |
| `vin cal` | the bus-sag cap, which keeps the slot connectors within their current rating, acts on the trimmed reading; trim it against a meter at the box (`vin` alone is allowed) |
| `stack`, `i2c`, `sim`, `button` | bench and test tools; `i2c` writes go around the port engine |

A new command that sets the token, gets past the update rules, clears the
settings, stops the firmware, trims a reading a safety cut-off acts on, or
is a bench or test tool goes on this list, in
`cli_line_web_refusal` (`src/cli_line.c`) and its test.
