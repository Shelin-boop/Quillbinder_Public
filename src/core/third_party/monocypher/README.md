# Monocypher (third-party code, unchanged)

Monocypher 4.0.3 by Loup Vaillant and contributors, a small cryptography library in C. Quillbinder uses only
its Ed25519 signature check (`crypto_ed25519_check`), to check the signatures of packages (0.65.0).

- Source: https://github.com/LoupVaillant/Monocypher, tag 4.0.3, commit ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f
- Files taken unchanged: `src/monocypher.c`, `src/monocypher.h`, `src/optional/monocypher-ed25519.c`,
  `src/optional/monocypher-ed25519.h`, `LICENCE.md`
- License: dual BSD-2-Clause or CC0-1.0 (see LICENCE.md); Quillbinder uses it under BSD-2-Clause.

SHA-256 of the files as taken:

    f1f838cdd483bdebe0df0ff5c5ed60535e496f769c6a2f933ac4c0b114207123  monocypher.c
    fcaf6ed771358bb4f40fba016f6518ae86ec02b1b877d2cc35ad92d3a26fd7b3  monocypher.h
    ce0d2f8e32ca8f66398ba5b3456cc74327c3eff14e7b950ce7d57be9025cc453  monocypher-ed25519.c
    3a3035181f991a158d0e1c7567258f0bae8ba0f1f23c5512b4a1db1b3c9730ce  monocypher-ed25519.h
    a5781770269d2516e52ba4863f790c10a16da4089a1e81823aee19ff1e9026b0  LICENCE.md
