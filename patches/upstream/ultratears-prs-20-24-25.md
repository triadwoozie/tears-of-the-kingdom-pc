# UltraTears upstream PR integration

`ultratears-prs-20-24-25.patch` carries the locally verified engine integration
for upstream PRs #24, #25, and the engine-only portion of #20. It is an
incremental patch against `triadwoozie/ultratears` commit
`1252c1b` (`dev-unified`); it is not a patch against this bridge repository.

The integration deliberately excludes PR #21 and the PyQt prelauncher files
from PR #20. The Tauri launcher remains the desktop launcher for this project.
The patch includes the Linux fixes needed for the shader-pack reader and
camera/input tests. Generated game C and build outputs are not included.

To apply it in an engine checkout at the stated base:

```sh
git apply --check --whitespace=error-all patches/upstream/ultratears-prs-20-24-25.patch
git apply --index patches/upstream/ultratears-prs-20-24-25.patch
```
