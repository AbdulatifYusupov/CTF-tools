# Stego Kripto Tool

Small educational tool that combines **steganography** and **post-quantum cryptography**.

Hide a message inside an image or audio file so that only the owner of the corresponding private key can extract it.

---

## What it does

1. **Encrypts** the payload with a hybrid scheme:
   - ML-KEM-768 (Kyber) for key encapsulation
   - ChaCha20-Poly1305 for authenticated encryption of the body
2. **Embeds** the result into a carrier using adaptive LSB (images) or LSB (audio)
3. **Extracts** and decrypts only with the matching private key

No magic bytes, no plaintext length in the clear. Header positions use a public seed; body positions are derived from the shared secret.

---

## Features

| Component | Details |
|-----------|---------|
| **Crypto** | ML-KEM-768 + ChaCha20-Poly1305 + HKDF-SHA256 |
| **Image stego** | Adaptive LSB (Sobel-based busy pixels, 1–2 bits/channel) |
| **Audio stego** | LSB on PCM samples (libsndfile) |
| **Error correction** | Reed-Solomon |
| **Analysis** | PSNR, SSIM, chi-square attack, LSB plane, amplified difference |
| **UI** | Qt6 worker (GUI still under construction) |
| **Language** | C++20 |

---

## Build

### Requirements

- CMake ≥ 3.21
- C++20 compiler
- **OpenSSL ≥ 3.5** (ML-KEM support)
- OpenCV (core, imgproc, imgcodecs)
- Qt6 ≥ 6.5 (Widgets + Concurrent)
- libsndfile

> Ubuntu 24.04 ships OpenSSL 3.0. You will need a custom build of OpenSSL 3.5+ and point CMake to it:
> ```bash
> cmake -DOPENSSL_ROOT_DIR=/opt/openssl-3.5 ..
> ```

### Commands

```bash
git clone https://github.com/AbdulatifYusupov/stego_kripto_tool.git
cd stego_kripto_tool
mkdir build && cd build
cmake ..
cmake --build .
ctest --output-on-failure   # optional
```

---

## Project structure

```
core/
  crypto/     # ML-KEM envelope + AEAD
  stego/      # Adaptive LSB, Audio LSB, Pipeline, PRNG, SlotCodec
  ecc/        # Reed-Solomon
  analysis/   # PSNR, SSIM, chi-square, etc.
ui/           # StegoWorker + (WIP) GUI
tests/        # Round-trip and unit tests
```

Core library has **no Qt dependency**, so it is easy to test and reuse.

---

## Status

Work in progress. Core crypto + stego + metrics + tests are in place. Full GUI is still being built.

Educational / research project — not production-ready and not audited.

---

## License

Currently no license file. All rights reserved for now.
