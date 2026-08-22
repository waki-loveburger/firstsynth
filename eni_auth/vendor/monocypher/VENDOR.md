# Monocypher (vendored)

Ed25519 signature verification for the licence check. Vendored rather than
linked so the plug-in has no external crypto dependency: two `.c` files that
compile into any target with no build-system work beyond adding them.

| | |
|---|---|
| Upstream | https://github.com/LoupVaillant/Monocypher |
| Version | **4.0.3** (tag `4.0.3`) |
| Licence | BSD-2-Clause OR CC0-1.0 (see the header of each file) |

Only the four files needed for `crypto_ed25519_check()` are kept:
`monocypher.c/.h` plus `optional/monocypher-ed25519.c/.h`. The `optional`
pair is what provides **RFC 8032 Ed25519** (EdDSA with SHA-512); the core
`crypto_eddsa_check()` uses BLAKE2b instead and would reject our tokens.

SHA-256 of the files as vendored:

```
f1f838cdd483bdebe0df0ff5c5ed60535e496f769c6a2f933ac4c0b114207123  monocypher.c
fcaf6ed771358bb4f40fba016f6518ae86ec02b1b877d2cc35ad92d3a26fd7b3  monocypher.h
ce0d2f8e32ca8f66398ba5b3456cc74327c3eff14e7b950ce7d57be9025cc453  monocypher-ed25519.c
3a3035181f991a158d0e1c7567258f0bae8ba0f1f23c5512b4a1db1b3c9730ce  monocypher-ed25519.h
```

Do not edit these files. To update, re-download from the tag and refresh the
hashes above.
