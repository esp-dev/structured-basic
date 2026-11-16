# Publishing the two repositories

Publish the core as `structured-basic` and the port as `structured-basic-stm32`
under the same GitHub owner. Publish the core first, then the port. The port's
relative submodule URL `../structured-basic` expects those sibling names.
If you choose another name or owner, update `.gitmodules` in the port first.

Clone the port using `--recurse-submodules` and check that Git resolves the
recorded core commit. Confirm the GitHub Actions results for both repositories.
The port workflow tests the native console, builds the STM32 firmware, and
uploads Intel HEX and BIN images with license notices.

Before changing repository visibility, review the reachable Git history and
the existing GitHub Actions logs and artifacts.

For release archives include the applicable license and notices. The port
lists SDK dependencies in `THIRD_PARTY.md`.
