# Wirelink documentation site

This directory contains the Astro and Starlight site published at
<https://docs.silkenkite.ink/wirelink/>.

Node.js 22.23.4 through 24.x is required; `.node-version` pins the CI version.
Install dependencies and start the local development server from this directory:

```sh
npm ci
npm run dev
```

Build and check the static site with:

```sh
npm run build
```

## Documentation checks

Scripts in `scripts/` check bilingual topic parity, local routes, repository/GitHub
paths and anchors, and tutorial commands against `docs/`. External service/repository
URLs are not fetched. Chinese checks enforce paragraph layout and terminology:

```sh
npm run check:docs   # CI: links, tutorial parity, Chinese paragraphs and terminology
npm run fix:docs     # expand hard-wrapped paragraphs to one line per paragraph
```

`check:docs` errors block CI. Warnings (negation stacking, half-translated terms)
need human review and do not block the build.

Deployment setup is documented in [DEPLOYMENT.md](DEPLOYMENT.md).
