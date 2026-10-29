// OmniLauncher Cloud: shared modpacks without accounts.
//
// A pack is a manifest (the launcher's custom manifest format) plus files the
// creator uploaded (configs zip, jars that aren't on Modrinth). Whoever holds the
// pack's editor key can publish; everyone with the code can read.
//
//   POST /api/packs                          → { code, editorKey }
//   PUT  /api/packs/:code          (key)     manifest JSON → { version }
//   PUT  /api/packs/:code/files/:sha256 (key) raw bytes   → { url }
//   GET  /p/:code                             current manifest
//   GET  /p/:code/files/:sha256               an uploaded file (HEAD works too)
//
// Storage is one KV namespace (PACKS): "p:<code>" metadata, "m:<code>" manifest,
// "f:<code>:<sha256>" files. Only the SHA-256 of the editor key is stored.

const CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const LIMITS = {
  manifestBytes: 512 * 1024,
  fileBytes: 24 * 1024 * 1024,      // KV values are capped at 25 MiB
  filesPerPack: 40,
  publishesPerDay: 30,
  uploadsPerDay: 60,
};
// Files a manifest may point at: Modrinth's CDN, or this pack's own uploads.
const ALLOWED_HOSTS = ["cdn.modrinth.com"];

export default {
  async fetch(request, env, ctx) {
    try {
      return await route(request, env, ctx);
    } catch (error) {
      return json({ error: "Erro interno." }, 500);
    }
  },
};

async function route(request, env, ctx) {
  const url = new URL(request.url);
  const parts = url.pathname.split("/").filter(Boolean);
  const method = request.method;

  if (method === "OPTIONS") return new Response(null, { status: 204, headers: cors() });

  if (parts[0] === "api" && parts[1] === "packs") {
    if (parts.length === 2 && method === "POST") return createPack(env);
    const code = normalizeCode(parts[2]);
    if (!code) return json({ error: "Código inválido." }, 400);
    if (parts.length === 3 && method === "PUT") return publish(request, env, ctx, code, url.origin);
    if (parts.length === 5 && parts[3] === "files" && method === "PUT") return upload(request, env, code, parts[4], url.origin);
    return json({ error: "Rota desconhecida." }, 404);
  }

  if (parts[0] === "p") {
    const code = normalizeCode(parts[1]);
    if (!code) return json({ error: "Código inválido." }, 400);
    if (parts.length === 2 && (method === "GET" || method === "HEAD")) return readManifest(request, env, ctx, code);
    if (parts.length === 4 && parts[2] === "files" && (method === "GET" || method === "HEAD")) return readFile(env, code, parts[3], method);
    return json({ error: "Rota desconhecida." }, 404);
  }

  if (parts.length === 0) return new Response("OmniLauncher Cloud", { headers: { "content-type": "text/plain; charset=utf-8" } });
  return json({ error: "Rota desconhecida." }, 404);
}

// ── Create ──────────────────────────────────────────────────────────────────

async function createPack(env) {
  for (let attempt = 0; attempt < 6; attempt++) {
    const code = randomCode(6);
    if (await env.PACKS.get("p:" + code)) continue;
    const editorKey = randomHex(32);
    const meta = { keyHash: await sha256Hex(new TextEncoder().encode(editorKey)), created: Date.now(),
                   version: 0, files: [], day: "", publishes: 0, uploads: 0 };
    await env.PACKS.put("p:" + code, JSON.stringify(meta));
    return json({ code, editorKey }, 201);
  }
  return json({ error: "Tente de novo." }, 503);
}

// ── Publish a manifest ──────────────────────────────────────────────────────

async function publish(request, env, ctx, code, origin) {
  const meta = await authorize(request, env, code);
  if (meta instanceof Response) return meta;
  const body = await request.arrayBuffer();
  if (body.byteLength > LIMITS.manifestBytes) return json({ error: "O manifesto é grande demais." }, 413);
  let manifest;
  try { manifest = JSON.parse(new TextDecoder().decode(body)); } catch { return json({ error: "Manifesto inválido." }, 400); }

  const problem = validateManifest(manifest, code, meta);
  if (problem) return json({ error: problem }, 400);
  if (!countToday(meta, "publishes", LIMITS.publishesPerDay)) return json({ error: "Limite de publicações por hoje atingido." }, 429);

  meta.version += 1;
  manifest.pack.revision = meta.version;
  manifest.pack.code = code;
  manifest.pack.publishedAt = new Date().toISOString();
  // Uploaded files are kept only while a published version uses them.
  const used = new Set();
  for (const file of manifest.files) { const sha = ownSha(file.url, code); if (sha) used.add(sha); }
  if (manifest.overrides) { const sha = ownSha(manifest.overrides.url, code); if (sha) used.add(sha); }
  const stale = meta.files.filter((sha) => !used.has(sha));
  meta.files = meta.files.filter((sha) => used.has(sha));

  await env.PACKS.put("m:" + code, JSON.stringify(manifest));
  await env.PACKS.put("p:" + code, JSON.stringify(meta));
  ctx.waitUntil(Promise.all(stale.map((sha) => env.PACKS.delete(`f:${code}:${sha}`))));
  ctx.waitUntil(caches.default.delete(new Request(origin + "/p/" + code)));
  return json({ version: meta.version, url: origin + "/p/" + code });
}

function validateManifest(manifest, code, meta) {
  if (!manifest || typeof manifest !== "object") return "Manifesto inválido.";
  const pack = manifest.pack;
  if (!pack || typeof pack.name !== "string" || !pack.name.trim() || pack.name.length > 80) return "Dê um nome de até 80 letras ao modpack.";
  if (typeof pack.version !== "string" || !pack.version.trim() || pack.version.length > 40) return "Versão inválida.";
  if (typeof manifest.minecraft !== "string" || !/^[0-9A-Za-z._\- ]{1,40}$/.test(manifest.minecraft)) return "Versão do Minecraft inválida.";
  const loaders = ["fabric", "forge", "neoforge"].filter((key) => manifest[key] && typeof manifest[key] === "object");
  if (loaders.length !== 1) return "O modpack precisa de um loader (Fabric, Forge ou NeoForge).";
  if (!Array.isArray(manifest.files) || manifest.files.length === 0 || manifest.files.length > 2000) return "Lista de arquivos inválida.";
  for (const file of manifest.files) {
    if (!file || typeof file.path !== "string" || !/^(mods|resourcepacks|shaderpacks|config)\/[^\\]+$/.test(file.path) || file.path.includes(".."))
      return "Caminho de arquivo não permitido: " + String(file && file.path);
    if (!allowedUrl(file.url, code, meta)) return "Arquivo de origem não permitida: " + file.path;
    if (!(typeof file.sha1 === "string" || typeof file.sha256 === "string")) return "Arquivo sem hash: " + file.path;
  }
  if (manifest.overrides !== undefined) {
    if (!manifest.overrides || !ownSha(manifest.overrides.url, code) || !allowedUrl(manifest.overrides.url, code, meta))
      return "As configurações precisam ser enviadas para o próprio pack.";
  }
  return "";
}

function allowedUrl(value, code, meta) {
  if (typeof value !== "string") return false;
  let url;
  try { url = new URL(value); } catch { return false; }
  if (url.protocol === "https:" && ALLOWED_HOSTS.includes(url.hostname)) return true;
  const sha = ownSha(value, code);
  return !!sha && meta.files.includes(sha);
}

// The sha256 of one of this pack's own uploads, from its URL.
function ownSha(value, code) {
  if (typeof value !== "string") return "";
  const match = value.match(/\/p\/([A-Z0-9]{6})\/files\/([0-9a-f]{64})$/);
  return match && match[1] === code ? match[2] : "";
}

// ── Upload a file ───────────────────────────────────────────────────────────

async function upload(request, env, code, sha, origin) {
  const meta = await authorize(request, env, code);
  if (meta instanceof Response) return meta;
  if (!/^[0-9a-f]{64}$/.test(sha)) return json({ error: "Hash inválido." }, 400);
  const url = `${origin}/p/${code}/files/${sha}`;
  if (meta.files.includes(sha)) return json({ url });
  if (meta.files.length >= LIMITS.filesPerPack) return json({ error: "Limite de arquivos do pack atingido." }, 413);
  const body = await request.arrayBuffer();
  if (body.byteLength === 0 || body.byteLength > LIMITS.fileBytes) return json({ error: "Arquivo vazio ou maior que 24 MB." }, 413);
  if ((await sha256Hex(body)) !== sha) return json({ error: "O arquivo não confere com o hash." }, 400);
  if (!countToday(meta, "uploads", LIMITS.uploadsPerDay)) return json({ error: "Limite de envios por hoje atingido." }, 429);
  await env.PACKS.put(`f:${code}:${sha}`, body, { metadata: { size: body.byteLength } });
  meta.files.push(sha);
  await env.PACKS.put("p:" + code, JSON.stringify(meta));
  return json({ url }, 201);
}

// ── Read ────────────────────────────────────────────────────────────────────

async function readManifest(request, env, ctx, code) {
  const cacheKey = new Request(new URL(request.url).origin + "/p/" + code);
  const cached = await caches.default.match(cacheKey);
  if (cached) return request.method === "HEAD" ? new Response(null, cached) : cached;
  const manifest = await env.PACKS.get("m:" + code);
  if (!manifest) return json({ error: "Nenhum modpack com esse código." }, 404);
  const response = new Response(manifest, {
    headers: { ...cors(), "content-type": "application/json; charset=utf-8", "cache-control": "public, max-age=300" },
  });
  ctx.waitUntil(caches.default.put(cacheKey, response.clone()));
  return request.method === "HEAD" ? new Response(null, response) : response;
}

async function readFile(env, code, sha, method) {
  if (!/^[0-9a-f]{64}$/.test(sha)) return json({ error: "Hash inválido." }, 400);
  const { value, metadata } = await env.PACKS.getWithMetadata(`f:${code}:${sha}`, { type: "arrayBuffer" });
  if (!value) return json({ error: "Arquivo não encontrado." }, 404);
  const headers = { ...cors(), "content-type": "application/octet-stream", "content-length": String((metadata && metadata.size) || value.byteLength),
                    "cache-control": "public, max-age=31536000, immutable" };
  return new Response(method === "HEAD" ? null : value, { headers });
}

// ── Helpers ─────────────────────────────────────────────────────────────────

async function authorize(request, env, code) {
  const raw = await env.PACKS.get("p:" + code);
  if (!raw) return json({ error: "Nenhum modpack com esse código." }, 404);
  const meta = JSON.parse(raw);
  const header = request.headers.get("authorization") || "";
  const key = header.startsWith("Bearer ") ? header.slice(7).trim() : "";
  if (!key || (await sha256Hex(new TextEncoder().encode(key))) !== meta.keyHash)
    return json({ error: "Chave de editor inválida." }, 403);
  return meta;
}

function countToday(meta, field, limit) {
  const today = new Date().toISOString().slice(0, 10);
  if (meta.day !== today) { meta.day = today; meta.publishes = 0; meta.uploads = 0; }
  if (meta[field] >= limit) return false;
  meta[field] += 1;
  return true;
}

function normalizeCode(value) {
  const code = String(value || "").toUpperCase().replace(/[^A-Z0-9]/g, "");
  return /^[A-Z0-9]{6}$/.test(code) ? code : "";
}

function randomCode(length) {
  const bytes = crypto.getRandomValues(new Uint8Array(length));
  return Array.from(bytes, (b) => CODE_ALPHABET[b % CODE_ALPHABET.length]).join("");
}

function randomHex(bytes) {
  return Array.from(crypto.getRandomValues(new Uint8Array(bytes)), (b) => b.toString(16).padStart(2, "0")).join("");
}

async function sha256Hex(data) {
  const digest = await crypto.subtle.digest("SHA-256", data);
  return Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, "0")).join("");
}

function cors() {
  return { "access-control-allow-origin": "*", "access-control-allow-headers": "authorization, content-type",
           "access-control-allow-methods": "GET, HEAD, POST, PUT, OPTIONS" };
}

function json(data, status = 200) {
  return new Response(JSON.stringify(data), { status, headers: { ...cors(), "content-type": "application/json; charset=utf-8" } });
}
