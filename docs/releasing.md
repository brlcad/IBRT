# Release Checklist

Use this checklist for beta tags and GitHub binary releases. A release candidate
is not ready merely because it builds on one maintainer workstation.

## Before Tagging

- Choose a semantic version such as `v0.1.0-beta.1` and move the relevant
  changelog entries from **Unreleased** into a dated version section.
- Confirm CI passes at the exact commit to be tagged.
- Build with clean `bext` and BRL-CAD install trees rather than relying on files
  left in an existing build directory.
- Run `ctest --test-dir <build-dir> --output-on-failure` on every platform being
  published.
- Exercise the viewer manually with at least `moss.g`, `havoc.g`, and `axis.g`.
- Verify orbit, pan, zoom, renderer switching, model reload, worker restart, and
  application shutdown.

## Binary Candidate Checks

- Test the archive on a clean machine or VM that does not have the build-time
  Qt, OSPRay, `bext`, or BRL-CAD trees installed.
- Confirm the archive contains `IBRT`, `IBRTRenderWorker`, the BRL-CAD OSPRay
  module, all non-system runtime libraries, demo models, `README.md`, `LICENSE`,
  `THIRD_PARTY_NOTICES.md`, and the exact license texts/notices supplied with
  every redistributed runtime dependency.
- Confirm no absolute build-machine paths are required at runtime.
- Confirm the archive does not contain tests, object files, debug-only runtime
  libraries, caches, crash dumps, or maintainer filesystem paths.
- Scan the final archive and record its SHA-256 checksum.

The Windows helper stages a candidate runtime after a Release build:

```powershell
pwsh apps/IBRT/package_ibrt_release.ps1 -BuildDir build/package-release
```

The helper creates a directory, not the final archive. Inspect and smoke-test the
directory before compressing it.

## Publishing

- Create an annotated beta tag from the tested commit.
- Publish the GitHub release as a **pre-release** and attach only the tested
  archives and checksum files.
- State the supported operating systems and architectures, known limitations,
  dependency versions, and whether binaries are signed/notarized.
- Install and launch each downloaded asset once after publication.
- Keep the previous beta available until the replacement assets have passed the
  post-publication check.
