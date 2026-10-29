# Shared and custom modpacks

The **Customizado** tab in Modpacks covers two ways of sharing a modpack. In both, followers get each new version as an incremental update: only new or changed files download, and worlds and personal files stay.

## Shared by code (OmniLauncher Cloud)

**Create.** "Criar modpack" turns a library game into a modpack with a six-letter code, and "Publicar versão" publishes a new version with optional release notes.

**What goes into a version:**
- Mods, shaders and resource packs found on Modrinth (looked up by SHA-1) go in as references: the Modrinth CDN URL plus the hash.
- `config/`, `defaultconfigs/` and `kubejs/` are zipped and uploaded as the pack's overrides.
- Jars that are not on Modrinth are uploaded too, up to 24 MB each and 40 files per pack.

**No accounts.** Creating a pack returns an editor key, which the launcher keeps with the game. An editor link (`omni-editor:<code>:<key>`) hands it to someone else, or to another PC.

**Join.** Paste the code, a pack link or an editor link into "Entrar".

### The server

`cloud/worker.js` is a Cloudflare Worker with one KV namespace (`PACKS`):

| Request | What it does |
| --- | --- |
| `POST /api/packs` | Creates a pack and returns `{ code, editorKey }` |
| `PUT /api/packs/:code` | Publishes a manifest (needs `Authorization: Bearer <editorKey>`) |
| `PUT /api/packs/:code/files/:sha256` | Uploads a file; the body must match the hash |
| `GET /p/:code` | The current manifest (cached for 5 minutes) |
| `GET /p/:code/files/:sha256` | An uploaded file |

Only the SHA-256 of an editor key is stored. Manifests may only point at `cdn.modrinth.com` or at the pack's own uploads. Uploads no longer used by the current version are deleted. Each pack is limited to 30 publishes and 60 uploads a day.

**Deploying.** The official instance runs at `https://cloud.omnilauncher.workers.dev`, on a Cloudflare account used only by OmniLauncher. To deploy your own:

```bash
cd cloud
npm install
npx wrangler login
npx wrangler kv namespace create PACKS   # paste the id into wrangler.toml
npx wrangler deploy                      # prints the worker URL
```

**Pointing the launcher at it.** Builds read the server address from `OMNI_CLOUD_URL` in CMake. An environment variable with the same name overrides it at runtime, for example to point at `npx wrangler dev`.

## Manifest by link

**What it is.** Any HTTPS URL that returns this manifest can be followed with "Entrar". Publishing a new version at the same URL makes followers see an update. It is the format the cloud serves, and the one the old Vanilla Ultra Enhanced launcher used.

```json
{
  "pack": { "name": "Vanilla Ultra Enhanced", "version": "1.1.0", "changelog": "optional" },
  "minecraft": "26.2",
  "fabric": { "loader": "0.19.3" },
  "files": [
    { "path": "mods/example.jar", "url": "https://…", "size": 123456, "sha256": "…" }
  ],
  "overrides": { "url": "https://…/configs.zip", "sha256": "…" }
}
```

**Fields:**
- **Loader:** `fabric` can be `forge` or `neoforge`, each with `"loader": "<version>"`.
- **Hashes:** `files[].sha1` is accepted in place of `sha256`.
- **`overrides`:** optional. A zip with `config/`, `defaultconfigs/` and the like, applied over the game on every install and update.
