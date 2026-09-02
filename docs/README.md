# Power Manifold docs

Documentation site for Power Manifold, built with [Astro](https://astro.build)
and [Starlight](https://starlight.astro.build). Published to
<https://mikesmitty.github.io/power-manifold/> by the `Docs` GitHub Actions
workflow on every push to `main` that touches `docs/`.

```sh
npm install
npm run dev      # http://localhost:4321/power-manifold/
npm run build    # static output in dist/
```

Pages are Markdown or MDX files under `src/content/docs/`. Every page needs a
`title` in its frontmatter; the sidebar is generated from the `hardware/` and
`firmware/` directories (see `astro.config.mjs`).
