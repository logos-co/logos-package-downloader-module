# Running This Package-Downloader Module Against logoscore

`logos-package-downloader-module` is the Logos `core` module that wraps the
[`logos-package-downloader`](https://github.com/logos-co/logos-package-downloader)
library (`lgpd`) and exposes the online package catalog and repository management as
callable module methods. This doc-test exercises **this** package-downloader-module
commit end-to-end through the headless `logoscore` runtime:

1. Build the `logoscore` CLI and the `lgpm` local package manager from their published
   flakes. `logoscore` is the headless frontend for `logos-liblogos`, so building it
   brings in the whole module-runtime stack.
2. Build **this** module as an installable `.lgx` package straight from its flake's
   `#lgx` output, **pinned to the commit under test** — so the module you run is built
   from exactly what is checked out here, not the latest published release.
3. Install the `.lgx` into a `./modules` directory with `lgpm`.
4. Start `logoscore` in daemon mode, load `package_downloader`, introspect it with
   `module-info`, and call its repository-management methods — verifying the module
   actually runs and round-trips real values.

These calls return a deterministic `"result"` envelope regardless of network state:
`listRepositories` triggers a best-effort metadata refresh but always returns, and
`addRepository("")` is rejected by pure validation before any fetch. (The bulk
catalog/download methods, which depend on the remote catalog being reachable, are
deliberately not asserted on here.) Because the module is built from the commit under
test and loaded through a real `logoscore` daemon, a green run is real evidence that
this change keeps the package-downloader module loadable and callable.

**What you'll build:** This `package_downloader` module, packaged as `.lgx`, installed with `lgpm`, and called through a `logoscore` daemon.

**What you'll learn:**

- How to build the `logoscore` runtime and the `lgpm` package manager from their flakes
- How a module's flake exposes a ready-to-install `.lgx` via its `#lgx` output
- How to install an `.lgx` into a modules directory with `lgpm`
- How to start the `logoscore` daemon, load a module, introspect it, and call its methods

## Prerequisites

- **Nix** with flakes enabled. Install from [nixos.org](https://nixos.org/download.html), then enable flakes:

```bash
mkdir -p ~/.config/nix
echo 'experimental-features = nix-command flakes' >> ~/.config/nix/nix.conf
```

Verify: `nix flake --help >/dev/null 2>&1 && echo "Flakes enabled"`

- **A Linux or macOS machine.** The methods called here are the module's repository-management calls, which return a deterministic result whether or not the network is reachable.

---

## Step 1: Build logoscore

Build the `logoscore` CLI from its published flake. The result is symlinked to
`./logos/`. `logoscore` is the headless frontend for `logos-liblogos`, so this one
build brings in the whole module-runtime stack the daemon needs.

### 1.1 Build the CLI

```bash
nix build 'github:logos-co/logos-logoscore-cli' --out-link ./logos
```

The build produces `logos/bin/logoscore` plus bundled runtime libraries and a
`logos/modules/` directory containing the built-in `capability_module` (required
for the auth handshake when loading modules).

---

## Step 2: Build the lgpm package manager

`lgpm` installs `.lgx` packages into a modules directory and scans what is
installed. Build it from the `logos-package-manager` flake and link it as `./lgpm`.

### 2.1 Build lgpm

```bash
nix build 'github:logos-co/logos-package-manager#cli' -o lgpm
```

The executable is at `./lgpm/bin/lgpm`.

---

## Step 3: Build and install this package-downloader module

Build **this** module's `.lgx` straight from its flake's `#lgx` output and install
it into a local `./modules` directory with `lgpm`. Every module built with
[`logos-module-builder`](https://github.com/logos-co/logos-module-builder) exposes a
ready-to-install `#lgx`.

> The `` in the URL pins the build to the commit under test: the doc-test
> runner expands it (locally this checkout's `HEAD` — see `run.sh`; in CI the commit
> being tested). With no pin it falls back to the latest `master`.

### 3.1 Build the module's .lgx

Build the `#lgx` output and link it as `./pkgdl-lgx`. (This compiles the module
and its dependencies through Nix, so the first build is slow.)

```bash
# From inside the clone this is simply: nix build '.#lgx'
nix build 'github:logos-co/logos-package-downloader-module#lgx' -o pkgdl-lgx
```

The `.lgx` package is now under `./pkgdl-lgx/`:

```bash
ls pkgdl-lgx/*.lgx
```

### 3.2 Seed the modules directory with the bundled capability module

`package_downloader` is loaded through the host's capability layer, so the
modules directory also needs the `capability_module` that ships with
`logoscore`. Copy it across first.

```bash
mkdir -p modules
cp -RL ./logos/modules/. ./modules/

```

### 3.3 Install the .lgx with lgpm

Install the freshly-built package into `./modules`. `package_downloader` is a
`core` module, so it goes to `--modules-dir`. The package is unsigned (a local
dev build), so we pass `--allow-unsigned`.

```bash
./lgpm/bin/lgpm --modules-dir ./modules --allow-unsigned install --file pkgdl-lgx/*.lgx
```

### 3.4 Confirm the install

Scan the directory and confirm the module landed:

```bash
./lgpm/bin/lgpm --modules-dir ./modules list
```

---

## Step 4: Run the daemon and call the module

Start `logoscore` in daemon mode pointed at `./modules`, then use the client
subcommands to load `package_downloader`, introspect it, and call its
repository-management methods. Daemon output is captured in `logs.txt`.

### 4.1 Start the daemon

Start logoscore in daemon mode in the background, capturing output to `logs.txt`:

```bash
logoscore -D -m ./modules > logs.txt &
```

The `-D` flag starts the daemon. The client subcommands below connect to this
running process via the config written under `~/.logoscore/`.

```bash
sleep 3
```

### 4.2 Check daemon status

Verify the daemon is running:

```bash
logoscore status
```

### 4.3 List discovered modules

`package_downloader` should be visible in the scan directory:

```bash
logoscore list-modules
```

### 4.4 Load the module

Load `package_downloader` into the running daemon:

```bash
logoscore load-module package_downloader
```

### 4.5 Confirm the module is loaded

Re-run `status`; the module that was `not_loaded` before now reports `loaded`:

```bash
logoscore status
```

### 4.6 Introspect the module with module-info

`module-info` lists the `Q_INVOKABLE` methods the module exposes — the same
methods you can `call`:

```bash
logoscore module-info package_downloader
```

### 4.7 List the configured repositories

`listRepositories` returns the repositories the catalog is assembled from — the
built-in default repository is always among them (its URL is compiled into the
wrapped `lgpd` library). The call triggers a best-effort metadata refresh of that
repository, but it returns deterministically whether or not the fetch succeeds, so
the `"result"` envelope is always present:

```bash
logoscore call package_downloader listRepositories
```

### 4.8 Reject an empty repository URL (pure validation, no network)

`addRepository` validates its argument before any fetch. Passing an empty URL is
rejected with `success: false` — a deterministic, offline validation round-trip:

```bash
logoscore call package_downloader addRepository ""
```

### 4.9 Stop the daemon

Shut the daemon down cleanly:

```bash
logoscore stop
```

The daemon removes its state file and exits.

```bash
sleep 2
```

### 4.10 Confirm the daemon has stopped

With no daemon running, the client reports `not_running` and exits non-zero, so
we add `|| true` to let the doc-test assert on the output:

```bash
logoscore status
```
