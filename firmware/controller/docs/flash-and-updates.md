# Flash layout and updates

How the controller's flash is laid out, how a board is programmed the first time, how firmware updates are delivered and verified, and how the controller programs the charger blades. The owner's view of updates is in [Updates](https://docs.powermanifold.io/guide/updates/).

## Partition table

The flash is divided by an RP2350 partition table into two A/B image slots,
selected by the bootrom, and a `data` partition holding persistent settings
and the fault log. Layouts live in `partitions/*.json`, one per board; the
build compiles the board's own (`PARTITION_TABLE_JSON` overrides it) into
`build/partition_table.uf2`. The controller card's 16 MB map,
`prod-16mb.json`:

| Offset | Size | Partition |
| --- | --- | --- |
| `0x000000` | 4K | partition table |
| `0x002000` | 4096K | `A` — image slot |
| `0x402000` | 4096K | `B` — image slot |
| `0x802000` | 7168K | `assets` |
| `0xF02000` | 1012K | `data` — settings ping-pong pair in the first two sectors |

The top 4 KB sector stays unpartitioned, as picotool requires for the
RP2350-E10 erratum workaround. The firmware never hardcodes these offsets:
it looks partitions up **by ID** through the bootrom at boot, so the same
code runs on any layout. The Pico 2 W stand-in's 4 MB map
(`pico2w-4mb.json`) has two 1536 KB image slots and a 1016 KB `data`
partition, and no `assets`. Boards with no partition table at all still
work — settings fall back to the legacy top-of-flash sectors and `info`
reports `slot raw`.

## First programming

Each board is programmed once. The partition table sits in the first
4 KB of flash and the firmware in an image slot behind it, so a new board
needs both.

- *Over SWD*, which is how a controller card is programmed (its USB is an
  unpopulated header): write `controller-factory.bin` at `0x10000000`. It is
  the start of flash as one file — the partition table, then the firmware
  where slot A begins.

  ```
  openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 4000" \
      -c "program build/controller-factory.bin 0x10000000 verify reset exit"
  ```

  Do not `program controller.elf` (or `controller.bin`) on a board meant to
  take updates: the firmware is linked for `0x10000000`, so that lands it
  where the partition table belongs, and the board then runs unpartitioned
  (`slot raw`) with nothing to update into. On a board that already has its
  partition table, `controller.bin` goes to a slot's own offset, `0x10002000`
  for slot A.
- *Over USB*: enter BOOTSEL and drag `partition_table.uf2`, then
  `controller.uf2`. The bootrom routes the app UF2 into an image slot by
  itself.

Settings saved by older raw-layout firmware are found and migrated into the
data partition on first boot.

## BOOTSEL updates

Dragging a newer `controller.uf2` in BOOTSEL lands in the
*inactive* slot, and the bootrom boots whichever slot holds the higher
image version (wired to `FW_VERSION`, so releases order themselves). Caveat
for local iteration: two builds with the *same* version tie-break to slot A —
either bump `FW_VERSION` locally or target a slot explicitly with
`picotool load -f -p <0|1> build/controller.uf2`.

## OTA push

Push the release's `controller.signed.bin` to the update endpoint
(a Pico 2 W stand-in takes `controller-pico2w.signed.bin`; the image names
its board and the other one refuses it):

```
curl --data-binary @controller.signed.bin http://<name>.local/api/v1/update
```

(add `-H "Authorization: Bearer <token>"` if an API token is set). The body
streams into the *inactive* slot with the try-before-you-buy flag forced on
the written image, gets verified by read-back, and the controller then
reboots into it as a trial. A half-finished or failed upload leaves nothing
bootable behind — the slot's first sector is erased before the transfer and
written last.

## OTA pull

The same pipeline also *pulls*: `update <http-url>` on the CLI fetches an
image over plain HTTP (any static file server, `python3 -m http.server`
included), and `update latest` fetches the newest release the controller
knows of.

## Update check

The controller looks for a newer release by itself. Half a
minute after the network comes up, and once a day after that, it fetches
`<update source>/controller/<board>/latest.json`, a pointer of the form
`{"version":"x.y.z","url":"http://.../controller.signed.bin"}`. A release
newer than the running one is announced on the console, shows in `info`, and
turns Home Assistant's update entity to *update available*. It installs
when asked for, there or with `update latest`, or by itself (see
[Automatic installs](#automatic-installs)). The update source is
`http://fw.powermanifold.io` unless set otherwise (`update source <url>`,
`update_url` in the settings, or the page), and `update source off` stops
the controller asking; `update check` asks at once. That host is a small
proxy in front of this repository's GitHub releases
(`firmware/update-proxy`). It exists because the update client supports only
plain HTTP (TLS is used only for the broker link and the web server) and GitHub serves release
downloads only over HTTPS. The same pointer can instead be published,
retained, to `pwrman/<name>/update/latest` by something on the LAN; the
newer of the two is the one that counts. Neither needs to be trusted, as the
next paragraph explains.

## Automatic installs

With `update_auto` on, the default, a newer release installs without being
asked (`src/update_auto.c` decides, `update_auto_poll` in
`src/net/update_check.c` acts):

- When a release first becomes known, the controller draws a wait of 1 to 7
  days (`get_rand_32`) and stores the version, the wait and the time it was
  seen (`update_seen`, `update_wait_days`, `update_seen_at`). The wait is
  counted by the clock, so a restart does not start it again; by uptime when
  the clock was not set at the time.
- Once the wait is over, it installs between 03:00 and 05:00 local time
  (`tz_offset_min`). Without a clock, it installs as soon as the wait is
  over.
- A pull that fails before the restart is tried again the next night.
- Before any restart into a new image, from any surface, the controller
  stores that image's version in `update_tried`. The next boot that is not
  a trial compares it with its own version: a match clears it, anything
  else means the trial was rolled back, and that version goes into
  `update_skip`, so it does not install by itself again.
- `update auto postpone` (`POST /api/v1/update/postpone`, the Home Assistant
  button) sets `update_postpone` to the time 7 days on; it needs the clock.
  `update auto skip` sets `update_skip` to the newest known release.
- No automatic install starts during a trial or while another transfer runs.

## Signed updates

An image is trusted on its signature alone, regardless of its source, so it
can be served over plain HTTP from any host.
`controller.signed.bin` is the raw `controller.bin` with a 176-byte trailer —
the image's length, the board it was built for, its SHA-512, and an Ed25519
signature over those — and the firmware carries the public keys in
`keys/*.pem`. Before the new slot is made bootable, the image has to be

- signed by one of the built-in keys,
- built for this board, and
- no older than the running firmware.

The push endpoint and the Home Assistant install cannot bypass these checks.
The console can: `update --unsigned <url>` takes an image without a trailer
(a local `controller.bin` or `.uf2`) and `update --downgrade <url>` an older
one, because reaching the console takes the unpopulated USB header or a
debug probe, and either of those can flash anything already. These checks do
not apply to BOOTSEL or picotool. A build with no keys in `keys/` says so
when it is configured, at boot and in `info`; it accepts unsigned images
from the network, and still refuses older ones.

The release workflow signs both released builds with a Cloud KMS key.
`tools/sign_image.py` does the signing (Cloud KMS, a YubiKey, a key file, or
any other signer in two steps),
`test/build/verify_image` checks a signed image with the firmware's own code
and keys, and [`keys/README.md`](../keys/README.md) covers adding and retiring
keys.

## Try-before-you-buy

A TBYB-flagged image boots as a *trial* — `info`
shows `slot B (TRIAL, uncommitted)` — and commits itself only after 10 s of
an unbroken engine heartbeat and, with WiFi configured, once the network
has come up at least once since boot; a link that then flaps does not
revert a good image. Until
then any reboot, watchdog reset, or the 10-minute deadline reverts to the
previous image, so a broken OTA push is undone automatically.

## Settings across a revert

The settings record only ever grows: a new
layout appends its fields after the old ones, so a record written by any
later firmware begins with everything an earlier one knows. Since 0.11 each
record also states its own length, which is where its crc sits, and a
firmware that meets a record of a layout it has never seen checks that crc
and reads the part it understands. The image a failed trial reverts to
therefore comes back with the WiFi, the names and the limits it had; its
next save writes its own layout, and the newer firmware defaults its own
fields again when it returns, as after any upgrade. An image on trial also
keeps every save to one of the two sectors, so the record the previous
firmware wrote is still there if the trial is reverted, and a save cut
short by a reset cannot take it. What a revert loses is at most what was
changed during the trial. Firmware before 0.11 reads no layout newer than
its own, so for the first update from it the one-sector rule is what keeps
the configuration: it finds its own record in the other sector.

## Version ordering

Pushing a **newer** build sticks by version comparison, and pushing a
strictly **older** one sticks too — the bootrom records the deliberate
downgrade and erases the newer slot's image when the trial commits, which is
the rollback path. Only pushing the **same** version does not reliably
persist across a power cycle (ties break to slot A). This occurs during
local iteration; bump `FW_VERSION` or use `picotool load -f -p <0|1>` at the
bench.

## Blade firmware updates

A gen-3 blade is programmed in the chassis, over the backplane, by the
controller: the STM32G0's ROM bootloader speaks its I2C protocol (AN4221)
on the same pins as the blade's register file, at 0x51, and this firmware
carries the blade firmware it was built with (`BLADE_IMAGE`, [Building](building.md#building);
`blades` on the console and `blade_fw` in the status JSON say which). The
blade side is in the [blade firmware docs](../../charger-module/docs/register-map.md#firmware-updates-over-the-backplane).

**When it happens.** The probe finds a blade in its bootloader — factory
blank, reset into it, or set to boot through it — and takes it through a
trip (`updating` in the state column, with a progress percentage): read the
bootloader's version and the chip's ID, read the image header out of the
flash, then either start what is there (it is the bundled version, and the
bootloader's checksum of the flash matches the bundle), or erase and write
the bundle, check it the same way and start it. That port is already dark,
so the trip interrupts no output.

A blade *running* its firmware has to be sent to the bootloader, and that
takes its port down for the few seconds the trip lasts. The controller does
this in two cases: the firmware is not the bundled version (always: the
chassis keeps its blades on the firmware it was tested with, downgrades
included, and there is no setting to turn that off, since a controller and
blades on different versions may not work together; to run a bench build
of the blade firmware, bundle it into a controller build), and its option
bytes are still the factory ones (`blades bootopt on`, the default, stored
as `blade_boot_via_loader` in the settings JSON), to have them programmed,
once, so every reset lands in the bootloader from then on. When both are
wanted the option goes first and the image is written under it, in the
same trip. Older settings JSON may carry `blade_auto_update`; it is
accepted and ignored.

**A port whose device is drawing is never taken down for either of them.**
They are done

- at a probe that starts from a dark port — a blade just seated, the
  chassis powering up, a port switched on, the retry after a fault — before
  the port is given power, a sink already plugged in or not;
- once a powered port has had nothing plugged in for ten seconds;
- once the device plugged in is switched off, which the controller sees as
  its draw staying under the charged threshold (`charged_mw` for
  `charged_min`, the same test that marks a device charged). The device
  loses power for the few seconds of the trip and is served again when the
  port comes back. With `charged_mw` at 0 the controller cannot tell a
  device is off, and this case never arises.

A port whose device stays on keeps its power on the firmware its blade has,
for days if that is how long the device stays on. The wait is made hard to
miss so the device gets switched off: the port's light flashes amber for
300 ms every 3 s over its own color (`led_pattern.c`), the problem list
(`health_problems`, so the page's status line, Home Assistant's *Problem*
sensor and `info`) says `blade update waiting: <ports>`, the page's port
card says how to let it run, `status` marks it `[blade update waiting]`,
and the status JSON and the telemetry carry `update_due` and the blade's
running version as `fw`. Home Assistant also gets a *Blade firmware*
update entity on `blades/state` (retained, published when it changes):
`latest_version` is the bundled version and `installed_version` the
oldest version a waiting blade runs, so Home Assistant lists an update
exactly while one waits. A waiting update whose blade already runs the
bundled version (only its boot option is due) or a newer one (the bundle
is a downgrade) does not show there, only in the problem list. Its
Install (`install` on `blades/set`) sends `port <n> update` to every
waiting port. `port <n> update` (the `update` action) forces an immediate
update: it rewrites the bundle onto the blade regardless of what is plugged
in or what firmware the blade runs.

The controller starts no blade update **while its own image is on trial**. A
controller update that carries a newer blade firmware changes no blade until
the trial has committed (ten seconds of health, see
[Try-before-you-buy](#try-before-you-buy)); one that never gets there — a
link too poor to count as healthy, a crash — is reverted with no blade
changed, so the previous image finds every blade on the firmware it expects.
(`blades` says so while it lasts.) A blade found in its bootloader during
the trial is started on what it holds.

**Why the boot option.** With it, any reset — the watchdog, a crash, a
brown-out — puts the blade where the controller can reach it, and a
rewrite that is interrupted at any point just starts over. Without it the
way into the bootloader is the flash's empty flag, and the bootloader
clears that flag when it starts (AN2606, 48.3.1): a reset halfway through
a rewrite then boots a flash with no first page, and the blade stays dead
until the chassis is switched off and on. That is why the option is set before the first
rewrite, and why the controller puts off its own restarts (an OTA reboot,
a trial revert) while a blade is mid-trip — the bootloader resets the
blade if the controller goes quiet for a second in the middle of a
command.

**The watch** (`blades watch`, `blade_watch_s`, default 120 s) is the way to
a blade whose firmware runs but has stopped answering. It is not a timeout
on the controller: a blade only resets into its bootloader when its EN is
low *and* it has not been addressed for that long. A controller that is
rebooting, updating or gone leaves EN where it was, and the ports run on.
`port <n> update` on a silent port is what uses it: EN goes low, the blade
resets into its bootloader within the watch time, and the retry after each
cooldown finds it there. Switching the port off and on does the same,
slower. `blades watch off` leaves only switching the chassis off and on.

**What can go wrong.** Each trip that ends with a blade back in its
bootloader without its firmware having come up as wanted counts; after three
the port is held in FAULT (`probe: update (crash loop)`) and stays there —
no retry every cooldown — until the chassis is switched off and on, the port
is re-enabled, or `port <n> update` is given. A blade that *does* run but
cannot be brought to what was wanted (an option that will not take) is not
faulted for it: after three tries the controller logs `probe: update (not
taking)` once, leaves the port in service and stops asking until one of
those three things happens. A trip that fails outright (`probe: update (no
image)` for a blank blade in a build without one, `(write)`, `(verify)`,
`(bootloader silent)`, `(wrong chip)`) is a probe failure like any other,
retried after the cooldown within the same allowance. The image is erased a
page at a time, first page first, and written first page last, its header
chunk last of all, so a blade that loses power midway reads as blank on the
next probe and is written again. A controller that reboots midway finds the
blade still in its bootloader and restarts the trip. (A page per erase
command with a pause after each is ST's workaround for bootloader V11.3,
which acknowledges an erase before the flash has finished; V11.4, the
current one, does not need it.) The work is sliced one step per engine tick,
so the other five ports keep being served; a whole image takes well under
ten seconds. Events: `update` with `written`, `started` or `boot option` and
the version in `text`; the HA events entity sees `updated` when an image was
written.
