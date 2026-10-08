---
title: Network settings
description: Fixed IP addresses, DNS, host names, the time server, an encrypted MQTT connection, syslog and the update source.
sidebar:
  order: 3
---

All of these are in the web interface's **Settings**. The defaults work on most
home networks, so you only need this page if you want something different.

## IP address

By default the controller gets its address from your router (DHCP). To give
it a fixed address, set **Addressing** to *static* and fill in the IP
address, netmask and gateway. The fixed address applies after a reboot.

With the Ethernet port in use, the fixed address goes on Ethernet and Wi-Fi
stays on DHCP. Without Ethernet it goes on Wi-Fi.

**DNS server** overrides the one your router hands out. Leave it blank to
use your router's, or the gateway's when the address is fixed.

## Host names

The controller answers only to its IP address and to its device name, such
as `pwrman.local`. A request that uses any other name gets a page saying the
controller does not answer to that name. This stops a website you visit from
reaching the controller through a DNS name of its own.

To open the controller by another name, add that name to **Host names**.
Examples include a name your router registers (such as `pwrman.lan` or
`pwrman.home.arpa`), a DNS record you created, a Tailscale MagicDNS name, or
the name of a reverse proxy that passes the original name through.
Separate several names with spaces. The IP address always works, so a
missing name can be added by opening the controller by its address.

Monitoring tools and scripts that reach the controller by name, such as a
Prometheus scrape of `/metrics`, need that name in **Host names** as well.

The names an installed [HTTPS](/integrations/https/) certificate covers are
accepted without being listed.

## Time server

The controller needs the time for the lights' night window, timestamps in
the fault log and a verified MQTT connection. By default it uses the time
server your router announces, and `time.cloudflare.com` if there isn't one
or it doesn't answer.

Set **Time server** if your network blocks outgoing NTP (UDP port 123) and
you have a local server.

## MQTT security

The MQTT connection is unencrypted (**plain**) by default. To encrypt it,
set **MQTT connection** to **TLS** and set the MQTT port to your broker's
TLS port, usually 8883.

With **TLS**, the controller checks that it is talking to your real broker:

- **A broker with a Let's Encrypt certificate** needs no further setup if
  you enter the broker by name, not by IP address.
- **Any other broker**, such as one with a self-signed certificate or your
  own certificate authority, needs its certificate. Paste the certificate
  authority that issued the broker's certificate, or the broker's own
  self-signed certificate, into **Broker certificate**, in PEM format, up
  to 2 KB.
- **A broker entered by IP address** always needs its certificate pasted
  in.

**TLS, unverified** encrypts the connection without verifying the broker's
identity. It prevents eavesdropping but not impersonation of the broker.

A verified connection waits until the controller has the time from the
[time server](#time-server), because certificate checks depend on the current
date.

## Syslog

Set **Syslog host** to copy the controller's messages to a syslog server
over UDP, port 514 by default. This includes the messages from before the
network came up.

## Update source

The controller asks `http://fw.powermanifold.io` once a day whether there is
new firmware. Clear **Update source** to stop it asking, or point it at
your own server that hosts the same files over plain HTTP; an `https://`
address is refused. See
[Firmware updates](/guide/updates/).
