# Stego Krypto Tool

Educational steganography + post-quantum cryptography tool.

Hides messages in images and audio using:
- **ML-KEM-768** (post-quantum) + **ChaCha20-Poly1305** authenticated encryption
- Adaptive LSB / LSB / Block DCT steganography

Built with **C++20**, Qt 6, OpenCV, libsndfile, OpenSSL 3.5.

## Features

- Two key modes:
  - **KEM** — recipient public/private key pair (ML-KEM-768)
  - **Password** — Argon2id derived key
- Algorithms: Adaptive LSB (images), LSB (audio), Block DCT (images)
- Reed-Solomon error correction
- Capacity estimation, steganalysis helpers (PSNR, SSIM, chi-square, LSB plane)
- CLI with profiles (so you don’t have to remember parameters)
- Async worker for future GUI

## Build (Debian / Kali / Ubuntu)

```bash
sudo apt install build-essential cmake pkg-config perl curl \
     libopencv-dev libsndfile1-dev qt6-base-dev libgl1-mesa-dev

bash scripts/build_openssl.sh          # OpenSSL 3.5.x → ~/.local (needed for ML-KEM)
export OPENSSL_ROOT_DIR=$HOME/.local/openssl-3.5.10
bash scripts/build.sh                  # configure + build + run tests
```

All 11 tests should pass:
`crypto_roundtrip`, `adaptive_lsb_roundtrip`, `audio_lsb_roundtrip`, `block_dct_roundtrip`,
`reed_solomon_test`, `metrics_test`, `steganalysis_test`, `stego_worker_test`,
`cli_args_test`, `cli_profile_test`, `cli_e2e_test`.

## CLI usage

```bash
# Generate key pair
build/stego_cli keygen --out mykey          # mykey.pub + mykey.key (keep .key secret)

# Check capacity
build/stego_cli capacity --algo block-dct --in photo.jpg

# Embed (KEM mode)
build/stego_cli embed --algo block-dct --in photo.jpg --out secret.png \
                      --pub mykey.pub --msg "hello" --progress

# Extract
build/stego_cli extract --algo block-dct --in secret.png --key mykey.key

# Analyze
build/stego_cli analyze --cover photo.jpg --stego secret.png \
                        --lsb-out lsb.png --diff-out diff.png

build/stego_cli help
```

### Password mode

```bash
echo 'correct horse battery staple' > pw.txt

build/stego_cli embed   --algo block-dct --in small.png --out s.png \
                        --password-file pw.txt --msg "hi"
build/stego_cli extract --algo block-dct --in s.png --password-file pw.txt
```

Use a long passphrase. Prefer `--password-file` or `--ask-password` — `--password` is visible via `ps`.

| Mode          | Header size | 256×256 block-dct | 1024×768 block-dct |
|---------------|-------------|-------------------|--------------------|
| KEM           | 1088 bytes  | almost nothing    | ~1.5 KB            |
| Password      | 16 bytes    | ~120 bytes        | ~2.6 KB            |

The file does **not** store which mode was used. The receiver must know.

### Profiles (recommended)

Parameters (`pairs`, `parity`, `threshold`, `bits`…) are **not** stored in the stego file.
Use a profile so you don’t have to remember them:

```bash
build/stego_cli embed --algo block-dct --pairs 3 --parity 24 \
                      --in a.png --out s.png --password-file pw.txt --msg "hi" \
                      --profile-out s.profile.json

build/stego_cli extract --profile s.profile.json --in s.png --password-file pw.txt
```

The profile contains algorithm + parameters + mode. Never keys or passwords.  
An explicit option that contradicts the profile is an error.

**Exit codes:** 0 = ok, 2 = bad args, 3 = no message / wrong key / damaged, 4 = doesn’t fit, 5 = I/O error.

## Important rules

- Images → always save as **PNG**
- Audio → **WAV / FLAC**
- Lossy re-encoding destroys the payload
- `bitsPerChannel` / `bitsPerSample`, `threshold`, `pairsPerBlock`, `paritySymbols` must be identical on embed and extract (they are effectively part of the key)

## Project layout

```
core/crypto     ML-KEM + HKDF + ChaCha20-Poly1305
core/ecc        Reed-Solomon over GF(256)
core/stego      AdaptiveLsb, AudioLsb, BlockDct, Pipeline…
core/analysis   PSNR, SSIM, chi-square, LSB plane
cli             stego_cli
ui              StegoWorker (async) + GUI stub
tests           one test per module
scripts         build helpers
```
