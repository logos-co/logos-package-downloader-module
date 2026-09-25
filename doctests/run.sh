#!/usr/bin/env bash
#
# Execute the package-downloader doc-test end-to-end and regenerate its Markdown.
#
# There are 2 specs:
#   package-downloader-storage.test.yaml — packages this module as an .lgx,
#       installs it with lgpm alongside the storage module, uploads it into a
#       local storage node, serves a catalog advertising its CID over local
#       HTTPS, and downloads it back through the storage network.
#   package-downloader-default-node.test.yaml — loads the downloader and the
#       storage module on an empty home, and checks the node the downloader
#       starts by itself, on the module's default configuration.
#
# The runner is the shared `doctest` CLI
# (https://github.com/logos-co/logos-doctest), invoked directly via its flake.
# Each spec runs into ./outputs/<spec>/ via --output-dir; `doctest generate` renders the
# .md; `doctest clean` then strips build artifacts, keeping only the .md.
#
# To run against a local logos-doctest checkout instead of the published flake,
# set DOCTEST, e.g.:  DOCTEST="nix run path:../../logos-doctest --" ./run.sh
#
set -euo pipefail

# Run from this doctests/ directory regardless of where the script is invoked from.
cd "$(dirname "$0")"

# The doctest CLI. Override by exporting DOCTEST (space-separated command).
read -r -a DOCTEST <<< "${DOCTEST:-nix run github:logos-co/logos-doctest --}"
OUTPUT_DIR="./outputs"

# Build the doc-test against THIS repo's current commit rather than the latest
# published flake. The spec pins `github:logos-co/logos-package-downloader-module{release}`
# to $COMMIT via --release-for, so the runtime spec packages exactly what is
# checked out here. Override by exporting COMMIT (e.g. a tag), or set COMMIT="" to
# fall back to latest master.
#
# Note: nix fetches the commit from the GitHub remote, so $COMMIT must be pushed
# to logos-co/logos-package-downloader-module. A local-only / uncommitted HEAD won't
# resolve; export COMMIT="" (or push first) in that case.
COMMIT="${COMMIT-$(git rev-parse HEAD)}"
RELEASE_FOR=()
if [ -n "${COMMIT}" ]; then
  RELEASE_FOR=(--release-for "logos-package-downloader-module=${COMMIT}")
  echo "==> Pinning logos-package-downloader-module to ${COMMIT}"
else
  echo "==> COMMIT empty; building from latest logos-package-downloader-module master"
fi

echo "==> Clearing previous ${OUTPUT_DIR}/"
# A prior run copies module artifacts out of the read-only nix store, so the
# directories land read-only (r-x) too. `rm -rf` can't delete files inside a
# directory it can't write to, so restore write permission first.
if [ -e "${OUTPUT_DIR}" ]; then
  chmod -R u+w "${OUTPUT_DIR}" 2>/dev/null || true
fi
rm -rf "${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}"

cleanup() {
  for dir in "${OUTPUT_DIR}"/*/; do
    "${dir}logosctl/bin/logosctl" --config-dir "${dir}session" stop > /dev/null 2>&1 || true
    for pid in "${dir}"*.pid; do
      kill "$(cat "${pid}")" 2> /dev/null || true
    done
  done
}
trap cleanup EXIT

status=0

# Run each spec into its own ./outputs/<spec>/, so they share no session or
# home. A failed spec does not stop the next one.
for spec in *.test.yaml; do
  name="$(basename "${spec%.test.yaml}")"
  echo "==> Running ${spec} into ${OUTPUT_DIR}/"
  # ${RELEASE_FOR[@]+...} guards the expansion so an empty array doesn't trip
  # `set -u` on older bash (e.g. macOS's stock 3.2).
  "${DOCTEST[@]}" run "${spec}" \
    --verbose \
    --continue-on-fail \
    ${RELEASE_FOR[@]+"${RELEASE_FOR[@]}"} \
    --output-dir "${OUTPUT_DIR}/${name}/" || status=1

  echo "==> Generating ${OUTPUT_DIR}/${name}.md"
  "${DOCTEST[@]}" generate "${spec}" \
    ${RELEASE_FOR[@]+"${RELEASE_FOR[@]}"} \
    -o "${OUTPUT_DIR}/${name}.md"
done

echo "==> Cleaning build artifacts from ${OUTPUT_DIR}/ (keeps .md)"
"${DOCTEST[@]}" clean "${OUTPUT_DIR}" --verbose

echo "==> Done. Rendered docs are in ${OUTPUT_DIR}/"
exit "${status}"
