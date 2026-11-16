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
npm run check:docs
npm run build
```

Deployment setup is documented in [DEPLOYMENT.md](DEPLOYMENT.md).

`check:docs` checks example-backed snippets, internal documentation routes and
single-answer quizzes. `build` type-checks Astro/MDX and creates the static site.
