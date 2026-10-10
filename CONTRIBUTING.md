# Contributing

This file describes how to report problems and propose changes. The
[README](README.md) describes the repository layout.

## Reporting problems

- Report security problems privately, as described in
  [SECURITY.md](SECURITY.md). Do not open a public issue or pull request
  for them.
- For other bugs, open an issue with the controller firmware version (shown
  on the web page and in Home Assistant), what happened and what you
  expected.
- Before starting a large change, open an issue to agree on the approach.

## Pull requests

- Base pull requests on `main` and keep each one to a single topic.
- Write commit messages and pull request titles as
  [Conventional Commits](https://www.conventionalcommits.org/):
  `type(scope): subject`, for example `fix(controller): ...` or
  `docs: ...`. release-please reads the types to choose each
  component's next version and to write its changelog. Use `feat` for new
  behavior, `fix` for a bug fix, and `docs`, `test`, `ci`, `refactor` or
  `chore` for changes that do not alter a released component.
- Use US English spelling in code, comments, docs and commit messages.
- Update the documentation in the same pull request as the change it
  describes (see [Documentation](#documentation)).

## Firmware

Build and test instructions are in each firmware's README and developer
docs:

- [Controller](firmware/controller/README.md) (RP2350) and
  [Building](firmware/controller/docs/building.md)
- [Charger module](firmware/charger-module/README.md) (STM32G071)

Before opening a pull request, run the host tests from the directory of the
firmware you changed:

```sh
cmake -S test -B test/build && cmake --build test/build
ctest --test-dir test/build --output-on-failure
```

CI builds the firmware and runs these tests on every pull request that
changes `firmware/controller/`, `firmware/charger-module/` or
`firmware/tools/`.

- Match the style of the surrounding code. Comments are plain, complete
  sentences.
- A change to the controller's HTTP API updates
  [`firmware/controller/docs/openapi.yaml`](firmware/controller/docs/openapi.yaml)
  in the same pull request. The `openapi` host test fails when the
  description and the web server disagree.
- Local builds trust the release signing keys in `firmware/controller/keys/`,
  so a network update refuses them. Install a local build over SWD, with
  picotool, or with `update --unsigned` on the console, as described in
  [Flash layout and updates](firmware/controller/docs/flash-and-updates.md).
- Never commit private keys.

## Hardware

Report a hardware problem or suggestion in an issue.

## Documentation

[`docs/README.md`](docs/README.md) explains where each page comes from: the
user guide is written in `docs/src/content/docs/`, and the firmware developer
pages in `firmware/*/docs/`.

- Write in a plain, neutral register.
- Describe the product as it is. Do not describe planned work.
- Owner pages cover only what an owner can reach: the web page, the HTTP
  API, MQTT and Home Assistant, and the front-panel button. They do not
  mention the serial console, SWD or BOOTSEL, and they never tell an owner
  to reseat a blade. Developer pages can cover all of these.
- Check electrical values against the schematics in `hardware/`.

## Licensing

By contributing, you agree that your contribution is licensed under the
license of the files it changes, as listed in the [README](README.md#license):
MIT for code, CC BY 4.0 for documentation and CERN-OHL-P-2.0 for hardware.

When you add a third-party file, such as a library, state its source and
license in the pull request.
