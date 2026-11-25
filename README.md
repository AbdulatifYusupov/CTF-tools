# CTF-tools

Коллекция утилит для CTF (Capture The Flag).

Каждый инструмент лежит в **отдельной ветке**.

## Инструменты

| Ветка | Описание |
|-------|----------|
| [`XOR-brute-decoder`](https://github.com/AbdulatifYusupov/CTF-tools/tree/XOR-brute-decoder) | XOR brute-force + cryptanalysis (C++17 / Qt 6). Single/multi-byte XOR, smart mode, known-plaintext, ADD/SUB, ROL/ROR и т.д. |
| [`stego_kripto_tool`](https://github.com/AbdulatifYusupov/CTF-tools/tree/stego_kripto_tool) | Steganography + post-quantum crypto (C++20). ML-KEM-768 + ChaCha20-Poly1305, Adaptive LSB / Block DCT / Audio LSB |

## Как пользоваться

```bash
git clone https://github.com/AbdulatifYusupov/CTF-tools.git
cd CTF-tools

# переключиться на нужный инструмент
git checkout XOR-brute-decoder
# или
git checkout stego_kripto_tool
```

## Структура

- `main` — индекс (этот README)
- остальные ветки — полноценные самостоятельные проекты
