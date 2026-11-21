# Stego Tool

C++20 / Qt 6 / OpenCV / libsndfile / OpenSSL 3.5. Authenticated envelope (HKDF + ChaCha20-Poly1305)
hidden with adaptive LSB (images), LSB (audio) or block DCT (images), in two key modes:
**KEM** (ML-KEM-768 post-quantum, recipient key pair) or **password** (Argon2id).

## First build (Debian / Kali / Ubuntu)

```bash
sudo apt install build-essential cmake pkg-config perl curl \
     libopencv-dev libsndfile1-dev qt6-base-dev libgl1-mesa-dev

bash scripts/build_openssl.sh       # OpenSSL 3.5.x into ~/.local (needed for ML-KEM)
export OPENSSL_ROOT_DIR=$HOME/.local/openssl-3.5.10
bash scripts/build.sh               # configure + build + run all tests
```

Expected: 9 tests, all `Passed`:
`crypto_roundtrip`, `adaptive_lsb_roundtrip`, `audio_lsb_roundtrip`, `block_dct_roundtrip`,
`reed_solomon_test`, `metrics_test`, `stego_worker_test`, `cli_args_test`, `cli_e2e_test`.

If something fails, paste the FIRST compiler error (or the failing test's output). Later errors are
usually fallout from the first one.

## Command line

```bash
build/stego_cli keygen --out mykey                       # mykey.pub + mykey.key (DER, keep .key secret)
build/stego_cli capacity --algo block-dct --in photo.jpg # how much fits
build/stego_cli embed   --algo block-dct --in photo.jpg --out secret.png \
                        --pub mykey.pub --msg "hello" --progress
build/stego_cli extract --algo block-dct --in secret.png --key mykey.key
build/stego_cli analyze --cover photo.jpg --stego secret.png --lsb-out lsb.png --diff-out diff.png
build/stego_cli help
```

### KEM mode or password mode

| | KEM (`--pub` / `--key`) | password (`--password-file`, `--ask-password`) |
|---|---|---|
| header in the file | 1088 bytes | 16 bytes |
| 256x256 image, block-dct | fits nothing | fits ~120 bytes |
| 1024x768 image, block-dct | ~1.5 KB | ~2.6 KB |
| attacker holding the file | cannot guess the key | can guess passwords offline (Argon2id, 64 MiB, 3 passes) |

The file does not record which mode was used (no magic bytes), so the receiver must know.
Use a long passphrase; `--password` on the command line is visible to other users via `ps`.
`capacity --mode password` shows how much fits.

```bash
echo 'correct horse battery staple' > pw.txt                      # or use --ask-password
build/stego_cli embed   --algo block-dct --in small.png --out s.png --password-file pw.txt --msg "hi"
build/stego_cli extract --algo block-dct --in s.png --password-file pw.txt
```

`embed` prints a profile line (`pairs=2 parity=32`, ...). Pass the same algorithm options to `extract`.
Exit codes: 0 ok, 2 bad arguments, 3 no message / wrong key / damaged file, 4 does not fit, 5 I/O error.

## Layout

```
core/crypto    ML-KEM + HKDF + ChaCha20-Poly1305 envelope
core/ecc       Reed-Solomon over GF(256)
core/stego     IStegoAlgorithm, AdaptiveLsb, AudioLsb, BlockDct, SlotCodec, Pipeline
core/analysis  PSNR, SSIM, chi-square, LSB plane
cli            stego_cli (keygen, capacity, embed, extract, analyze)
ui             StegoWorker (async), GUI stub
tests          one test per module
```

## Rules that matter

- Images: always save as **PNG**. Audio: **WAV/FLAC**. Any lossy re-encode destroys the payload.
- `bitsPerChannel`/`bitsPerSample`, `threshold`, `pairsPerBlock`, `paritySymbols` must be identical at
  embed and extract (they are effectively part of the key). `BlockDct::strength` is encoder-only.
