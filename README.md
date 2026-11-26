# CTF-tools

Cryptanalysis and steganography utilities. Each tool lives in its own branch.

### Available Tools

* **[XOR-brute-decoder](https://github.com/AbdulatifYusupov/CTF-tools/tree/XOR-brute-decoder)** (`C++17`, `Qt 6`)  
  XOR brute-force and cryptanalysis: single/multi-byte XOR, smart mode, known-plaintext attacks, `ADD`/`SUB`, `ROL`/`ROR`.

* **[stego_kripto_tool](https://github.com/AbdulatifYusupov/CTF-tools/tree/stego_kripto_tool)** (`C++20`)  
  Steganography + post-quantum cryptography: `ML-KEM-768` + `ChaCha20-Poly1305`, Adaptive LSB, Block DCT, Audio LSB.

### Usage

```bash
git clone https://github.com/AbdulatifYusupov/CTF-tools.git
cd CTF-tools

# Switch to the tool you need
git checkout XOR-brute-decoder
# or
git checkout stego_kripto_tool
```
