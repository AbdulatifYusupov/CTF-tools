# xorbrute: XOR Cipher Cryptanalysis Desktop Tool for CTF

A high-performance, cross-platform desktop application and CLI tool developed in **C++17** and **Qt 6** designed for cryptanalysis, frequency analysis, and cracking of XOR ciphers in CTF (Capture The Flag) competitions.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Qt 6](https://img.shields.io/badge/Qt-6.x-green.svg)
![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)

---

## Key Features

1. **Flexible Data Input**:
   - Supports direct text pasting and **safe drag-and-drop** of files.
   - **Binary-Safe**: Raw files are preserved in memory without UTF-8 string encoding corruption.
   - **Automatic Encoding Detection**: Smart detection prioritizing Hex over Base64 (e.g. `deadbeef` is correctly parsed as Hex) and Raw bytes fallback.

2. **Cracking & Cryptanalysis Modes**:
   - **Single-Byte XOR**: Instant brute force across all 256 keys (`0x00..0xFF`).
   - **Multi-Byte XOR**: Parallelized exhaustive search for key lengths 2 and 3 ($256^2$ and $256^3$ keys) using `QtConcurrent` across CPU cores.
   - **Smart Mode (Transposition & Frequency Analysis)**: Solves repeating-key XOR for key lengths 4 to 32+ by transposing ciphertext columns and independently ranking byte frequencies per position.
   - **Key Length Auto-Detection**: Automatically estimates key lengths using Cryptopals-style normalized Hamming distance and Index of Coincidence (IoC).
   - **Known-Plaintext & Crib Dragging**: Searches for known prefixes (e.g., `FLAG{`, `CTF{`) anywhere in the ciphertext via sliding window analysis.

3. **High-Performance Architecture**:
   - **Min-Heap Top-N (Top 200)**: Avoids allocating decrypted text or preview strings during brute force; only materializes surviving candidates.
   - **Zero-Allocation Hot Loops**: Precomputed 256-element lookup table for character scoring with raw pointer iterations.
   - **Responsive GUI**: Crypto operations execute in a background `QThread` with throttled progress reporting (`16,384` key batches) to prevent UI stalls.

4. **Results & UI/UX**:
   - High-speed `QTableView` with `QAbstractTableModel` and numeric sorting.
   - "Only with FLAG" filter and dynamic substring search.
   - Double-click inspection dialog with **Plaintext**, **Hex Dump**, and **Python `b"..."` escaped repr**.
   - Export options: "Copy Result", "Save to File", "Export all to CSV".
   - Modern Dark Theme (Catppuccin Mocha inspired) by default, with toggleable Light Theme.
   - Hotkeys: `Ctrl+Enter` (Run), `Ctrl+C` (Copy selected), `Ctrl+S` (Save file), `Esc` (Cancel).

---

## Build Instructions

### Prerequisites
- CMake >= 3.16
- C++17 compliant compiler (GCC 9+, Clang 10+, or MSVC 2019+)
- Qt 6 (Core, Widgets, Concurrent, Test)

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
For headless environments, scripts, and terminal pipelines:
```bash
# Single-byte brute force (auto-detects base64/hex)
./build/xorbrute_cli -i "RU9CRC43aWdxNDsxaWtiNTFpYk9PMDs6NDFS"

# Multi-byte exhaustive brute force (2 or 3 bytes)
./build/xorbrute_cli -i "414243444546" -e hex -m multi -k 2

# Estimate probable key lengths using Hamming distance & IoC
./build/xorbrute_cli -i "..." --detect-keylen

# Smart transposition mode for longer keys (e.g. 8 bytes)
./build/xorbrute_cli -f ciphertext.bin -e raw -m smart -k 8

# Known-plaintext crib dragging
./build/xorbrute_cli -i "RU9CRC43aWdxNDsxaWtiNTFpYk9PMDs6NDFS" -m known -c "FLAG-"
```

---

## Verification Test Vector

The project includes unit tests verifying the CTF challenge test vector:

- **Input (Base64)**: `RU9CRC43aWdxNDsxaWtiNTFpYk9PMDs6NDFS`
- **Auto-detected Encoding**: Base64
- **Best Candidate Key**: `0x03`
- **Decrypted Flag**: `FLAG-4jdr782jha62jaLL38972Q`

### Running Automated Tests
```bash
# Using ctest
ctest --test-dir build --output-on-failure

# Using custom CMake target
cmake --build build --target tests
```

---

## Project Structure
```
XOR_Brute_tool/
├── CMakeLists.txt              # Root build configuration
├── README.md                   # Project documentation
├── src/
│   ├── core/                   # Decoupled business logic (No GUI)
│   │   ├── Candidate.h         # Candidate & FastCandidate structures
│   │   ├── EncodingUtils.h/cpp # Base64, Hex, Raw encoders & hex dump
│   │   ├── Scorer.h/cpp        # Fast LUT scoring, markers, chi-squared
│   │   └── XorEngine.h/cpp     # Single, multi, smart, and crib dragging
│   ├── gui/                    # Qt6 Widgets GUI
│   │   ├── CandidateTableModel.h/cpp # Virtualized table model with numeric sort
│   │   ├── DropAreaTextEdit.h/cpp    # Binary-safe drag & drop text edit
│   │   ├── MainWindow.h/cpp          # Main splitter UI and worker thread
│   │   ├── ResultViewerDialog.h/cpp  # Candidate detail viewer (Hex/Repr)
│   │   ├── SettingsDialog.h/cpp      # Configurable scoring weights & theme
│   │   └── Theme.h/cpp               # Dark/Light theme stylesheets
│   ├── app/
│   │   └── main.cpp            # GUI application entry point
│   └── cli/
│       └── cli_main.cpp        # CLI application entry point
└── tests/
    ├── CMakeLists.txt          # Test build target
    └── test_xortool.cpp        # QtTest test suite
```

---

## License
MIT License. Created for CTF enthusiasts and educational cryptography analysis.
