# Deploy Wirelink documentation

GitHub Actions checks every documentation pull request and deploys `main` to
Cloudflare Workers Static Assets. Same-repository pull requests upload a Worker
version with a preview URL. Fork pull requests run the build without receiving
deployment credentials.

## Create a scoped Cloudflare token

In the Cloudflare dashboard, open **My Profile → API Tokens → Create Token** and
start from **Edit Cloudflare Workers**. Restrict the token to the account that
owns the Worker and to the `silkenkite.ink` zone.

The resulting token needs these permissions:

- Account → Workers Scripts → Edit
- Zone → Workers Routes → Edit, restricted to `silkenkite.ink`

Copy the token when Cloudflare shows it. Do not place it in a file or command
argument.

## Add GitHub configuration

From a terminal authenticated with GitHub CLI, run:

```sh
gh secret set CLOUDFLARE_API_TOKEN --repo starwey604/wirelink
```

Paste the token at the hidden prompt. GitHub encrypts it before storage.

Find the Cloudflare account ID on the account overview page. It identifies the
account but does not authorize access, so store it as a repository variable:

```sh
gh variable set CLOUDFLARE_ACCOUNT_ID \
  --repo starwey604/wirelink \
  --body 'your-32-character-account-id'
```

Confirm that both names exist without printing the secret value:

```sh
gh secret list --repo starwey604/wirelink
gh variable list --repo starwey604/wirelink
```

## Deployment behavior

- Pull requests build and validate the site.
- Pull requests from branches in this repository also receive a public
  `workers.dev` preview URL.
- A push to `main` deploys production to
  `https://docs.silkenkite.ink/wirelink/`.
- A `v*` tag validates the documentation at that tag without replacing the
  production deployment.
- `workflow_dispatch` can repeat the checks and deploy when run from `main`.

The first production deployment creates the `docs.silkenkite.ink` custom domain,
DNS record, and certificate. An existing DNS record with the same hostname must
be removed or replaced before that deployment.
