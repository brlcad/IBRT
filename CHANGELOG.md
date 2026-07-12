# Changelog

## Unreleased

- Prepared the project metadata, documentation, and Windows packaging path for the
  first `0.1.0` beta.
- Reshaped the repo into an app-and-plugins layout rooted at `apps/` and `plugins/`.
- Removed the vendored OSPRay source tree and upstream-only superbuild/test content.
- Switched the build contract to `BEXT_INSTALL_DIR` plus `BRLCAD_PREFIX`.
- Renamed the viewer location to `apps/IBRT` and kept local deployment of `ospray_module_brl_cad`.
