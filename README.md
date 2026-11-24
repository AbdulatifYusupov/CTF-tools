# xorbrute: XOR Cipher & CTF Cryptanalysis Tool

<p align="center">
  <img src="IconOnly_Transparent.png" alt="xorbrute logo" width="110" />
</p>

A high-performance, cross-platform desktop application and CLI tool developed in **C++17** and **Qt 6** designed for cryptanalysis, frequency analysis, and brute-forcing of XOR and classic byte-level ciphers in CTF (Capture The Flag) competitions.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Qt 6](https://img.shields.io/badge/Qt-6.x-green.svg)
![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)

---

## Key Features

1. **Flexible Data Input & Encodings**:
   - Supports direct text pasting and **binary-safe drag-and-drop** of files (raw bytes stored in `QByteArray` without UTF-8 corruption).
   - **Supported Encodings & Auto-Detection**:
     - **Hex** (prioritized over Base64/Base32, e.g. `deadbeef` is recognized as Hex)
     - **Base64**
     - **Base32** (RFC 4648 `A-Z2-7=`)
     - **Base85 / ASCII85** (Adobe `<~...~>` with `z` zero-block support and RFC 1924 / Python `b85`)
     - **URL-decode** (`%XX` percent-encoding, binary-safe)
     - **Raw Bytes**
   - **Reverse Bytes** option (`--reverse` / checkbox) to reverse decoded byte order before decryption.

2. **Cracking & Cryptanalysis Modes**:
   - **Single-Byte XOR**: Instant brute force across all 256 keys (`0x00..0xFF`).
   - **Multi-Byte XOR (Exhaustive)**: Parallelized search for key lengths 2 and 3 ($256^2$ and $256^3$ keys) using `QtConcurrent` across all CPU cores.
   - **Smart Mode (Transposition & Frequency Analysis)**: Solves repeating-key XOR for key lengths 2 to 32+ by transposing ciphertext columns and ranking byte frequencies per position.
   - **Key Length Auto-Detection**: Estimates probable key lengths using Cryptopals-style normalized Hamming distance and Index of Coincidence (IoC).
   - **Known-Plaintext & Crib Dragging**: Searches for known substrings (e.g., `FLAG{`, `CTF{`) anywhere in the ciphertext via sliding window analysis.
   - **ADD / SUB & ROT-N / Caesar**: Tests all 256 byte shifts `(b - k) & 0xFF`, Caesar `ROT-1..25` on letters, and `ROT-47` on printable ASCII.
   - **Bitwise Shifts (`ROL / ROR` 1–7 bits) + `NOT`**: Tests `~b`, `ROL(1..7)`, `ROR(1..7)`, and their combinations with single-byte XOR in both orders.
   - **Combo Mode (`XOR + ADD/ROL`)**: Parallelized brute force of `(b ^ k1) - k2`, `(b - k2) ^ k1` (65,536 × 2 combinations), and `XOR + ROL/ROR`.

3. **High-Performance Architecture**:
   - **Min-Heap Top-N (Top 200)**: Avoids allocating decrypted text or preview strings during brute force; only materializes surviving candidates.
   - **Zero-Allocation Hot Loops**: Precomputed 256-element lookup table for character scoring with raw pointer iterations.
   - **Responsive GUI**: Crypto operations execute in a background `QThread` with batched progress reporting (`16,384` key batches) and instant cancellation.

4. **Results & UI/UX**:
   - `QTableView` with `QAbstractTableModel`, numeric sorting, `"Only with FLAG"` filter, and live search.
   - Double-click inspection dialog with **Plaintext**, **Hex Dump**, and **Python `b"..."` escaped repr**.
   - Export options: **Copy Result**, **Save to File**, **Export all to CSV**.
   - Modern Dark Theme (Catppuccin Mocha inspired) by default, with toggleable Light Theme (`QSettings`).
   - Hotkeys: `Ctrl+Enter` (Run), `Ctrl+C` (Copy selected), `Ctrl+S` (Save file), `Esc` (Cancel).

---

## Build Instructions

### Prerequisites
- CMake >= 3.16
- C++17 compliant compiler (GCC 9+, Clang 10+, or MSVC 2019+)
- Qt 6 (`Core`, `Widgets`, `Concurrent`, `Test`)

### Linux (Debian/Ubuntu/Kali)
```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev
cmake -B build
cmake --build build
```

### Windows (MSVC / MinGW)
```cmd
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.x/msvc2019_64"
cmake --build build --config Release
```

### macOS
```bash
brew install qt cmake
cmake -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
```

---

## Running the Application

### GUI Application (`xorbrute`)
```bash
./build/xorbrute
```

### CLI Application (`xorbrute_cli`)
```bash
# Single-byte XOR (auto-detects Base64/Hex/Base32/Base85/URL)
./build/xorbrute_cli -i "RU9CRC43aWdxNDsxaWtiNTFpYk9PMDs6NDFS"

# Multi-byte exhaustive brute force (2 or 3 bytes)
./build/xorbrute_cli -i "414243444546" -e hex -m multi -k 2

# Estimate probable key lengths using Hamming distance & IoC
./build/xorbrute_cli -f ciphertext.bin -e raw --detect-keylen

# Smart transposition mode for longer keys (e.g. 8 bytes)
./build/xorbrute_cli -f ciphertext.bin -e raw -m smart -k 8

# Known-plaintext crib dragging
./build/xorbrute_cli -i "RU9CRC43aWdxNDsxaWtiNTFpYk9PMDs6NDFS" -m known -c "FLAG-"

# Byte ADD/SUB & ROT-N / Caesar
./build/xorbrute_cli -i "SYNT{ebg13_grfg}" -m addrot

# Bitwise NOT & ROL/ROR (1-7 bits) + XOR
./build/xorbrute_cli -f payload.bin -e raw -m bitwise

# Combo mode (XOR + ADD/SUB & XOR + ROL/ROR) with reversed bytes
./build/xorbrute_cli -f payload.bin -e raw -r -m combo
```

---

## Verification Test Vector

- **Input (Base64)**: `RU9CRC43aWdxNDsxaWtiNTFpYk9PMDs6NDFS`
- **Auto-detected Encoding**: `Base64`
- **Best Candidate Key**: `0x03`
- **Decrypted Flag**: `FLAG-4jdr782jha62jaLL38972Q`

### Running Automated Tests
```bash
# Using ctest
ctest --test-dir build --output-on-failure

# Using custom CMake target (14/14 tests)
cmake --build build --target tests
```

---

## Project Structure
```
XOR_Brute_tool/
├── CMakeLists.txt                # Root build configuration
├── resources.qrc                 # Embedded Qt resources (logo)
├── IconOnly_Transparent.png      # Application icon
├── README.md                     # Project documentation
├── src/
│   ├── core/                     # Decoupled business logic (No GUI)
│   │   ├── Candidate.h           # Candidate, FastCandidate, TransformOp
│   │   ├── EncodingUtils.h/cpp   # Hex, Base64, Base32, Base85, URL, Reverse
│   │   ├── Scorer.h/cpp          # Fast LUT scoring, markers, chi-squared
│   │   └── XorEngine.h/cpp       # All XOR, ADD/ROT, Bitwise, and Combo engines
│   ├── gui/                      # Qt6 Widgets GUI
│   │   ├── CandidateTableModel.h/cpp
│   │   ├── DropAreaTextEdit.h/cpp
│   │   ├── MainWindow.h/cpp
│   │   ├── ResultViewerDialog.h/cpp
│   │   ├── SettingsDialog.h/cpp
│   │   └── Theme.h/cpp
│   ├── app/
│   │   └── main.cpp              # GUI application entry point
│   └── cli/
│       └── cli_main.cpp          # CLI application entry point
└── tests/
    ├── CMakeLists.txt
    └── test_xortool.cpp          # QtTest suite (14 tests)
```
