<div align="center">

<h1 align="center"> Tala </h1>

[![Release build](https://img.shields.io/github/actions/workflow/status/mKonic/tala/release.yml?labelColor=27303D&label=Release&labelColor=06599d&color=043b69)](https://github.com/mKonic/tala/actions/workflows/release.yml)
[![Release](https://img.shields.io/github/v/release/mKonic/tala.svg?maxAge=3600&label=Release&labelColor=06599d&color=043b69)](https://github.com/mKonic/tala/releases/latest)
[![License: GPL-3.0](https://img.shields.io/github/license/mKonic/tala?labelColor=27303D&color=0877d2)](/LICENSE)

<div align="left">

A dictation app for Linux: hold a key, speak, and the text is typed where your cursor is, with
spoken commands, a dictionary, snippets and styles. It brings
[Wispr Flow](https://wisprflow.ai)'s and [Detta](https://fermionresearch.com/products/detta)'s
way of dictating to Wayland, running [Phonon-2](https://huggingface.co/FermionResearch/Phonon-2) on
your own computer.

## Use

Releases are attached to their tag rather than published to the AUR, so they install with pacman
from the release assets.

```sh
curl -LO https://github.com/mKonic/tala/releases/download/v0.1.0/tala-0.1.0-1-x86_64.pkg.tar.zst
sudo pacman -U tala-0.1.0-1-x86_64.pkg.tar.zst
```

## License

GPL-3.0-or-later. See [LICENSE](./LICENSE).
