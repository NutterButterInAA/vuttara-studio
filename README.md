# Vuttara Studio

Vuttara Studio is a new Windows streaming application under clean-room local
development.

## Current baseline

- Version: `0.0.1`
- Language: C++20
- Interface: Qt 6 Widgets
- Media engine: libobs, to be integrated in a later isolated stage
- Platform: Windows x64
- Status: local development only

## Foundation Stage 1

This stage validates:

- a new source tree with no legacy implementation files;
- Qt 6 Widgets configuration and compilation;
- approved branding resources;
- local logging and application data paths;
- Windows deployment with `windeployqt`;
- a noninteractive self-test;
- clean local Git initialization without a remote or commit.

Libobs is intentionally not integrated in this stage. The next stage will add a
pinned, reproducibly staged libobs runtime behind the Vuttara engine layer.
