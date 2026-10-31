# Arch package

Build the pinned `wlroots0.18` dependency first, then install it and build
`PKGBUILD`. Set
`ARCHINTOSH_VERSION` to the upstream ZacOS 9 release version and
`ARCHINTOSH_COMMIT` to the fork commit to package that release:

```sh
cd packaging/arch/wlroots0.18 && makepkg -s
sudo pacman -U wlroots0.18-0.18.3-2-x86_64.pkg.tar.zst
cd ..
ARCHINTOSH_VERSION=1.6.2 ARCHINTOSH_COMMIT=<fork-commit> makepkg -s
```

The compositor requires wlroots 0.18. `wlroots0.18/PKGBUILD` builds that ABI
against current Arch libraries and includes the source compatibility fixes
needed for current libinput and GCC. Its wlroots source archive and patches
are SHA-256 checked.

The release workflow polls upstream after the fork's upstream-sync workflow,
then attaches an `x86_64` package and checksum to the matching release in the
personal fork. It only packages a release after its source commit is present
in the fork's synchronized `main` branch.

The Arch build of Software Update checks the latest release in
`gazecube/Archintosh-OS-9`, downloads the matching package asset, verifies its
GitHub SHA-256 digest and package metadata, then installs it with `pacman -U`
through `pkexec`. Arch package version comparison uses `vercmp`.

The Software Manager still uses Debian package metadata and needs a separate
Arch backend.
