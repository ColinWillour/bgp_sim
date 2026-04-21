# BGP Simulator — WebAssembly Setup Guide

## Overview

1. Install Emscripten (the C++ → WASM compiler)
2. Compile the simulator to WebAssembly
3. Push the site to GitHub
4. Deploy to Cloudflare Pages (free hosting with automatic HTTPS)
5. (Optional) Buy a custom domain

---

## Step 1 — Install Emscripten

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh   # add to ~/.bashrc or ~/.zshrc to make it permanent
```

Verify the installation:

```bash
emcc --version
# emcc (Emscripten gcc/clang-like replacement + linker emulating GNU ld) 3.x.x
```

---

## Step 2 — Compile C++ to WebAssembly

Your project directory should look like this before building:

```
bgp_sim/
├── src/
│   ├── main.cpp
│   ├── Network.cpp
│   ├── Router.cpp
│   ├── BGP.cpp
│   └── Route.cpp
├── include/
│   ├── Types.h
│   ├── Route.h
│   ├── Router.h
│   ├── Policy.h
│   ├── BGP.h
│   ├── ROV.h
│   └── Network.h
└── bgp_web/           ← this folder you just created
    ├── SETUP.md
    ├── build_wasm.sh
    ├── wrangler.toml
    ├── wasm_src/
    │   └── bgp_wasm.cpp
    └── public/
        ├── index.html
        └── _headers
```

From the **`bgp_sim/` root**, run:

```bash
bash bgp_web/build_wasm.sh
```

This produces two files:

```
bgp_web/public/bgp_simulator.js    ← JavaScript glue code
bgp_web/public/bgp_simulator.wasm  ← compiled C++ binary
```

---

## Step 3 — Push to GitHub

From `bgp_sim/bgp_web/` (or whatever folder you want as the repo root):

```bash
git init
git add .
git commit -m "Initial commit: BGP Simulator WebAssembly site"
git remote add origin https://github.com/YOUR_USERNAME/bgp-simulator.git
git push -u origin main
```

> Make sure `bgp_simulator.js` and `bgp_simulator.wasm` are committed — they are
> the compiled output that Cloudflare will serve.

---

## Step 4 — Deploy to Cloudflare Pages

1. Go to [dash.cloudflare.com](https://dash.cloudflare.com) and log in.
2. In the sidebar, click **Workers & Pages**.
3. Click **Create** → **Pages** → **Connect to Git**.
4. Authorize GitHub and select your repository.
5. Configure the build settings:
   - **Framework preset:** None
   - **Build command:** *(leave blank)*
   - **Build output directory:** `public`
6. Click **Save and Deploy**.

Cloudflare will deploy your site and give you a URL like
`https://bgp-simulator.pages.dev`.

---

## Step 5 — Buy a Domain (Optional)

1. In Cloudflare, go to **Domain Registration** and search for a domain (~$10/year).
2. Complete the purchase.
3. Go to your Pages project → **Custom domains** → **Set up a custom domain**.
4. Enter your domain and follow the prompts (DNS is configured automatically
   since the domain is already in Cloudflare).

---

## Step 6 — Test It

1. Open your deployed URL (or `file://` won't work — use Pages or a local server).
2. Drop in your AS relationships `.txt` file.
3. Drop in your announcements `.csv` file.
4. Optionally drop in an ROV ASNs file.
5. Enter a Target ASN number.
6. Click **Run Simulation**.
7. Verify results appear in the table and the download button works.

---

## Troubleshooting

**"SharedArrayBuffer is not defined"**
> The `_headers` file must be deployed alongside your site. Cloudflare Pages
> reads it automatically. If missing, add it to `bgp_web/public/_headers` and
> redeploy. Without the `Cross-Origin-Embedder-Policy` and
> `Cross-Origin-Opener-Policy` headers, WASM threads cannot access shared memory.

**Simulation hangs or crashes on large files**
> The default build allocates 256 MB of WASM memory. For very large topology
> files, increase `INITIAL_MEMORY` in `build_wasm.sh` (must be a multiple of
> 65536). For example: `-s INITIAL_MEMORY=536870912` for 512 MB. Then recompile
> and redeploy.

**bgp_simulator.js not found (404)**
> The compiled output files were not committed to git. Run `build_wasm.sh`,
> then `git add bgp_web/public/bgp_simulator.js bgp_web/public/bgp_simulator.wasm`
> and push again.

---

## Re-deploying After Changes

After editing C++ source files, recompile and push:

```bash
source /path/to/emsdk/emsdk_env.sh   # if not in your shell profile
bash bgp_web/build_wasm.sh
git add bgp_web/public/bgp_simulator.js bgp_web/public/bgp_simulator.wasm
git commit -m "Recompile WASM"
git push
```

Cloudflare Pages will automatically redeploy within ~30 seconds.
