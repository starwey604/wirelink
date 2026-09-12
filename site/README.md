# Wirelink documentation site

This directory contains the Astro and Starlight site published at
<https://docs.silkenkite.ink/wirelink/>.

Node.js 22.19 through 24.x is required; `.node-version` pins the CI version.
Install dependencies and start the local development server from this directory:

```sh
npm ci
npm run dev
```

Build and check the static site with:

```sh
npm run build
```

## Chinese documentation checks

The rules in [`../docs/documentation-style-guide-cn.md`](../docs/documentation-style-guide-cn.md)
are enforced by scripts in `scripts/`:

```sh
npm run check:docs   # CI: hard-wrapped paragraphs, curly quotes, term/phrasing hints
npm run fix:docs     # expand hard-wrapped paragraphs to one line per paragraph
```

`check:docs` errors block CI. Warnings (negation stacking, half-translated terms)
need human review and do not block the build.

The repository root also exposes the same build through CMake:

```sh
cmake -S . -B build/docs -DWIRELINK_BUILD_DOCS=ON
cmake --build build/docs --target wirelink_docs
```

Deployment setup is documented in [DEPLOYMENT.md](DEPLOYMENT.md).
