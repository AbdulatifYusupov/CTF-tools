# CTF-tools

Cryptanalysis and steganography utilities. Each tool is maintained in a separate branch.

### Available Tools

* **[`XOR-brute-decoder`](https://github.com/AbdulatifYusupov/CTF-tools/tree/XOR-brute-decoder)** (`C++17`, `Qt 6`) — XOR brute-force and cryptanalysis: single/multi-byte XOR, smart mode, known-plaintext attacks, `ADD`/`SUB`, `ROL`/`ROR`.
* **[`stego_kripto_tool`](https://github.com/AbdulatifYusupov/CTF-tools/tree/stego_kripto_tool)** (`C++20`) — steganography and post-quantum cryptography: `ML-KEM-768` + `ChaCha20-Poly1305`, Adaptive LSB, Block DCT, Audio LSB.

### Usage

```bash
git clone [https://github.com/AbdulatifYusupov/CTF-tools.git](https://github.com/AbdulatifYusupov/CTF-tools.git)
cd CTF-tools

# Switch to the desired tool branch
git checkout XOR-brute-decoder
# or
git checkout stego_kripto_tool
