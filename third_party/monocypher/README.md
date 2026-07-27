# Monocypher 4.0.2

This directory vendors the unmodified Monocypher 4.0.2 core and optional
RFC 8032 Ed25519 implementation from the
[official release](https://monocypher.org/download/monocypher-4.0.2.tar.gz).
RBF-Safe uses only the Ed25519 key-pair, signing, verification, and secure-wipe
entry points.

The upstream archive SHA-256 is
`38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c`.
The vendored file SHA-256 values are:

- `monocypher.c`:
  `afe2b098c8569577a84488e0b98d276d1fba6506adea68bb9241a52111734c59`
- `monocypher.h`:
  `f78bb31255cfb7beba66afd2137f5194c8a025cf40488b6cc1e295234d43f374`
- `optional/monocypher-ed25519.c`:
  `7c9b16056cbd27521919e8a6f56a228808b9e718afc42e3d33f28c08e5abdee2`
- `optional/monocypher-ed25519.h`:
  `bd546edcd468d64e28caa3dbf4b1d6bfad7435c0ce994723fd81aae26405121b`
- `LICENCE.md`:
  `5f8360e4c06ddcc584bdb4b210c6af824c4bb301e6a9a521869b6d90795ca4b3`

Monocypher is dual-licensed under BSD-2-Clause or CC0-1.0. The complete
upstream terms are retained in `LICENCE.md` and in each source file. These
files are deliberately excluded from RBF-Safe formatting rewrites.
