#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QTextStream>
#include <QEventLoop>
#include <iostream>
#include <iomanip>

#include "../core/EncodingUtils.h"
#include "../core/Scorer.h"
#include "../core/XorEngine.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("xorbrute_cli"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Fast XOR cipher cryptanalysis and brute-force tool for CTFs"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption inputOption(QStringList() << "i" << "input",
                                   QStringLiteral("Input string to decrypt."),
                                   QStringLiteral("string"));
    parser.addOption(inputOption);

    QCommandLineOption fileOption(QStringList() << "f" << "file",
                                  QStringLiteral("Input file path containing ciphertext."),
                                  QStringLiteral("filepath"));
    parser.addOption(fileOption);

    QCommandLineOption encodingOption(QStringList() << "e" << "encoding",
                                      QStringLiteral("Encoding type: auto, hex, base64, raw (default: auto)."),
                                      QStringLiteral("type"),
                                      QStringLiteral("auto"));
    parser.addOption(encodingOption);

    QCommandLineOption modeOption(QStringList() << "m" << "mode",
                                  QStringLiteral("Mode: single, multi, smart, known (default: single)."),
                                  QStringLiteral("mode"),
                                  QStringLiteral("single"));
    parser.addOption(modeOption);

    QCommandLineOption keyLenOption(QStringList() << "k" << "keylen",
                                    QStringLiteral("Key length in bytes (2-3 for multi, 2-32 for smart)."),
                                    QStringLiteral("len"),
                                    QStringLiteral("2"));
    parser.addOption(keyLenOption);

    QCommandLineOption cribOption(QStringList() << "c" << "crib",
                                  QStringLiteral("Known plaintext crib for known mode (e.g. FLAG{)."),
                                  QStringLiteral("crib"));
    parser.addOption(cribOption);

    QCommandLineOption topOption(QStringList() << "t" << "top",
                                 QStringLiteral("Number of top candidates to display (default: 10)."),
                                 QStringLiteral("N"),
                                 QStringLiteral("10"));
    parser.addOption(topOption);

    QCommandLineOption detectKeyLenOption(QStringList() << "d" << "detect-keylen",
                                          QStringLiteral("Estimate and print top candidate key lengths using Hamming/IoC."));
    parser.addOption(detectKeyLenOption);

    parser.process(app);

    // Read input data
    QByteArray rawData;
    QString inputText;

    if (parser.isSet(fileOption)) {
        QFile file(parser.value(fileOption));
        if (!file.open(QIODevice::ReadOnly)) {
            std::cerr << "Error: Could not open file: " << parser.value(fileOption).toStdString() << "\n";
            return 1;
        }
        rawData = file.readAll();
        inputText = QString::fromUtf8(rawData);
    } else if (parser.isSet(inputOption)) {
        inputText = parser.value(inputOption);
    } else {
        std::cerr << "Error: Either -i/--input or -f/--file must be specified. Run with -h for help.\n";
        return 1;
    }

    // Determine encoding
    QString encStr = parser.value(encodingOption).toLower();
    EncodingType encType = EncodingType::Auto;
    if (encStr == "hex") encType = EncodingType::Hex;
    else if (encStr == "base64" || encStr == "b64") encType = EncodingType::Base64;
    else if (encStr == "raw") encType = EncodingType::Raw;

    if (encType == EncodingType::Auto) {
        encType = EncodingUtils::detectEncoding(inputText);
        std::cout << "[*] Auto-detected encoding: " << EncodingUtils::encodingName(encType).toStdString() << "\n";
    }

    QByteArray ciphertext = EncodingUtils::decodeInput(inputText, encType, rawData);
    if (ciphertext.isEmpty()) {
        std::cerr << "Error: Decoded ciphertext is empty.\n";
        return 1;
    }

    std::cout << "[*] Ciphertext size: " << ciphertext.size() << " bytes\n";

    // Handle --detect-keylen
    if (parser.isSet(detectKeyLenOption)) {
        std::cout << "[*] Estimating key lengths via Normalized Hamming Distance & IoC:\n";
        auto estimates = XorEngine::estimateKeyLengths(ciphertext, 32);
        for (int i = 0; i < std::min(5, static_cast<int>(estimates.size())); ++i) {
            std::cout << "  Rank #" << (i + 1) << ": Key Length = " << estimates[i].first
                      << " (Score: " << std::fixed << std::setprecision(2) << estimates[i].second << ")\n";
        }
    }

    int topN = parser.value(topOption).toInt();
    if (topN <= 0) topN = 10;

    QString mode = parser.value(modeOption).toLower();
    XorEngine engine;
    QEventLoop loop;
    QVector<Candidate> finalResults;

    QObject::connect(&engine, &XorEngine::statusMessage, [](const QString &msg) {
        std::cout << "[*] " << msg.toStdString() << "\n";
    });

    QObject::connect(&engine, &XorEngine::error, [&loop](const QString &err) {
        std::cerr << "[-] Error: " << err.toStdString() << "\n";
        loop.quit();
    });

    QObject::connect(&engine, &XorEngine::finished, [&loop, &finalResults](const QVector<Candidate> &results) {
        finalResults = results;
        loop.quit();
    });

    if (mode == "single") {
        engine.runSingleByte(ciphertext, topN);
    } else if (mode == "multi") {
        int keyLen = parser.value(keyLenOption).toInt();
        if (keyLen < 2 || keyLen > 3) {
            std::cerr << "Error: Exhaustive multi-byte mode only supports key lengths 2 or 3.\n";
            return 1;
        }
        engine.runMultiByte(ciphertext, keyLen, topN);
    } else if (mode == "smart") {
        int keyLen = parser.value(keyLenOption).toInt();
        if (keyLen < 1) keyLen = 4;
        engine.runSmartMultiByte(ciphertext, keyLen, topN);
    } else if (mode == "known") {
        if (!parser.isSet(cribOption)) {
            std::cerr << "Error: --crib must be specified for known-plaintext mode.\n";
            return 1;
        }
        QByteArray crib = parser.value(cribOption).toUtf8();
        int keyLen = parser.value(keyLenOption).toInt();
        engine.runKnownPlaintext(ciphertext, crib, keyLen, true, topN);
    } else {
        std::cerr << "Error: Unknown mode: " << mode.toStdString() << "\n";
        return 1;
    }

    if (finalResults.isEmpty()) {
        loop.exec();
    }

    if (finalResults.isEmpty()) {
        std::cout << "No candidates found.\n";
        return 0;
    }

    // Print table
    std::cout << "\n"
              << std::left << std::setw(6)  << "Rank"
              << std::left << std::setw(16) << "Key (Hex)"
              << std::left << std::setw(10) << "Score"
              << std::left << std::setw(8)  << "Flag"
              << "Preview\n";
    std::cout << std::string(75, '-') << "\n";

    int count = std::min(topN, static_cast<int>(finalResults.size()));
    for (int i = 0; i < count; ++i) {
        const auto &cand = finalResults[i];
        std::cout << std::left << std::setw(6)  << cand.rank
                  << std::left << std::setw(16) << cand.key.toHex().toUpper().toStdString()
                  << std::left << std::setw(10) << std::fixed << std::setprecision(1) << cand.score
                  << std::left << std::setw(8)  << (cand.containsFlag ? "[YES]" : "")
                  << cand.preview.toStdString() << "\n";
    }

    return 0;
}
