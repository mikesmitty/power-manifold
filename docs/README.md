# Power Manifold docs

Documentation site for Power Manifold, built with [Astro](https://astro.build)
and [Starlight](https://starlight.astro.build). Published to
<https://mikesmitty.github.io/power-manifold/> by the `Docs` GitHub Actions
workflow on every push to `main` that touches `docs/` or `firmware/*/docs/`.

```sh
npm install
npm run dev      # http://localhost:4321/power-manifold/
npm run build    # static output in dist/
```

## Where pages come from

- **User guide** (`setup/`, `guide/`, `integrations/`) and the hardware
  architecture (`developers/architecture.md`) are written here, under
  `src/content/docs/`. Every page needs a `title` in its frontmatter.
- **Firmware developer pages** are written next to the firmware, in
  `firmware/controller/docs/` and `firmware/charger-module/docs/`, as plain
  GitHub Markdown. `scripts/sync-firmware-docs.mjs` copies them into
  `src/content/docs/developers/` before every `dev` and `build` (`npm run
  sync` on its own). Those copies are ignored by git; edit the source files.
  A new file needs an entry in the script's `PAGES` list.

The sidebar is generated from the directories (see `astro.config.mjs`).
