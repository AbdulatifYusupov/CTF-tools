#include <QTest>
#include <QSignalSpy>
#include "../src/core/EncodingUtils.h"
#include "../src/core/Scorer.h"
#include "../src/core/XorEngine.h"

class TestXorTool : public QObject {
    Q_OBJECT

private slots:
    void testEncodingDetection();
    void testDecoding();
    void testScorerPrintable();
    void testScorerFlagBonus();
    void testTestVectorSingleByte();
    void testKeyLengthEstimation();
    void testCribDragging();
    void testMultiByteBruteForce();
    void testExtendedEncodings();
    void testAddSubRot();
    void testBitwiseShiftNot();
    void testComboXorAddRol();
};

void TestXorTool::testEncodingDetection() {
    // Hex priority test (deadbeef could be b64 or hex, must detect as Hex)
    QCOMPARE(EncodingUtils::detectEncoding(QStringLiteral("deadbeef")), EncodingType::Hex);
    QCOMPARE(EncodingUtils::detectEncoding(QStringLiteral("0xCAFE1337")), EncodingType::Hex);
    QCOMPARE(EncodingUtils::detectEncoding(QStringLiteral("41424344")), EncodingType::Hex);

    // Base64
    QCOMPARE(EncodingUtils::detectEncoding(QStringLiteral("RU9CRC43aWdxNDsxaWtiNTFpYk9PMDs6NDFS")), EncodingType::Base64);
    QCOMPARE(EncodingUtils::detectEncoding(QStringLiteral("SGVsbG8gV29ybGQh")), EncodingType::Base64);
    QCOMPARE(EncodingUtils::detectEncoding(QStringLiteral("YQ==")), EncodingType::Base64);

    // Raw fallback
    QCOMPARE(EncodingUtils::detectEncoding(QStringLiteral("This is plain raw text.")), EncodingType::Raw);
}

void TestXorTool::testDecoding() {
    // Hex decode
    QByteArray hexDec = EncodingUtils::decodeInput(QStringLiteral("414243"), EncodingType::Hex);
    QCOMPARE(hexDec, QByteArray("ABC"));

    // Base64 decode
    QByteArray b64Dec = EncodingUtils::decodeInput(QStringLiteral("SGVsbG8="), EncodingType::Base64);
    QCOMPARE(b64Dec, QByteArray("Hello"));

    // Auto decode on Base64
    QByteArray autoDec = EncodingUtils::decodeInput(QStringLiteral("SGVsbG8="), EncodingType::Auto);
    QCOMPARE(autoDec, QByteArray("Hello"));
}

void TestXorTool::testScorerPrintable() {
    Scorer scorer;
    QByteArray printableText = "Hello, CTF player! Welcome to the competition.";
    QByteArray binaryGarbage("\x00\x01\xFF\xFE\x02\x03\x80\x81\x82\x83", 10);

    bool hasFlag = false;
    double scoreGood = scorer.calculateDetailedScore(printableText, hasFlag);
    double scoreBad = scorer.calculateDetailedScore(binaryGarbage, hasFlag);

    QVERIFY(scoreGood > scoreBad);
    QVERIFY(scoreGood > 0.0);
    QVERIFY(scoreBad < 0.0);
}

void TestXorTool::testScorerFlagBonus() {
    Scorer scorer;
    QByteArray regularText = "The quick brown fox jumps over the lazy dog";
    QByteArray flagText = "FLAG{this_is_a_secret_flag_value}";

    bool flagFound1 = false;
    bool flagFound2 = false;

    scorer.calculateDetailedScore(regularText, flagFound1);
    double scoreWithFlag = scorer.calculateDetailedScore(flagText, flagFound2);

    QVERIFY(!flagFound1);
    QVERIFY(flagFound2);
    QVERIFY(scoreWithFlag > 100.0);
}

void TestXorTool::testTestVectorSingleByte() {
    // Target CTF vector specified in requirements:
    // Input: "RU9CRC43aWdxNDsxaWtiNTFpYk9PMDs6NDFS" -> key 0x03 -> "FLAG-4jdr782jha62jaLL38972Q"
    QString inputStr = QStringLiteral("RU9CRC43aWdxNDsxaWtiNTFpYk9PMDs6NDFS");

    EncodingType detected = EncodingUtils::detectEncoding(inputStr);
    QCOMPARE(detected, EncodingType::Base64);

    QByteArray ciphertext = EncodingUtils::decodeInput(inputStr, detected);
    QVERIFY(!ciphertext.isEmpty());

    XorEngine engine;
    QSignalSpy spy(&engine, &XorEngine::finished);

    engine.runSingleByte(ciphertext, 256);

    QCOMPARE(spy.count(), 1);
    QVector<Candidate> results = qvariant_cast<QVector<Candidate>>(spy.at(0).at(0));
    QVERIFY(!results.isEmpty());

    // Rank 1 must be key 0x03
    const Candidate &best = results.first();
    QCOMPARE(best.rank, 1);
    QCOMPARE(best.key.size(), 1);
    QCOMPARE(static_cast<uint8_t>(best.key.at(0)), static_cast<uint8_t>(0x03));
    QCOMPARE(best.plaintext, QByteArray("FLAG-4jdr782jha62jaLL38972Q"));
    QCOMPARE(best.preview, QStringLiteral("FLAG-4jdr782jha62jaLL38972Q"));
    QVERIFY(best.containsFlag);
}

void TestXorTool::testKeyLengthEstimation() {
    // Generate repeated XOR with 4-byte key "KEY!"
    QByteArray key = "KEY!";
    QByteArray plain = "Cryptography is the practice and study of techniques for secure communication in the presence of adversarial third parties. Flag is CTF{xor_cracked}.";
    QByteArray cipher;
    cipher.resize(plain.size());
    for (int i = 0; i < plain.size(); ++i) {
        cipher[i] = plain[i] ^ key[i % key.size()];
    }

    auto estimates = XorEngine::estimateKeyLengths(cipher, 16);
    QVERIFY(!estimates.isEmpty());

    // Key length 4 (or multiple like 8) should be in the top 3
    bool found4 = false;
    for (int i = 0; i < std::min(3, static_cast<int>(estimates.size())); ++i) {
        if (estimates[i].first == 4 || estimates[i].first == 8) {
            found4 = true;
            break;
        }
    }
    QVERIFY(found4);
}

void TestXorTool::testCribDragging() {
    QString inputStr = QStringLiteral("RU9CRC43aWdxNDsxaWtiNTFpYk9PMDs6NDFS");
    QByteArray ciphertext = EncodingUtils::decodeInput(inputStr, EncodingType::Base64);

    XorEngine engine;
    QSignalSpy spy(&engine, &XorEngine::finished);

    // Search with crib "FLAG-"
    engine.runKnownPlaintext(ciphertext, QByteArray("FLAG-"), 1, true, 50);

    QCOMPARE(spy.count(), 1);
    QVector<Candidate> results = qvariant_cast<QVector<Candidate>>(spy.at(0).at(0));
    QVERIFY(!results.isEmpty());

    const Candidate &best = results.first();
    QCOMPARE(static_cast<uint8_t>(best.key.at(0)), static_cast<uint8_t>(0x03));
    QCOMPARE(best.plaintext, QByteArray("FLAG-4jdr782jha62jaLL38972Q"));
}

void TestXorTool::testMultiByteBruteForce() {
    // 2-byte key: 0x13, 0x37
    QByteArray key("\x13\x37", 2);
    QByteArray plain = "FLAG{fast_multibyte_xor_bruteforce_works_well!}";
    QByteArray cipher;
    cipher.resize(plain.size());
    for (int i = 0; i < plain.size(); ++i) {
        cipher[i] = plain[i] ^ key[i % 2];
    }

    XorEngine engine;
    QSignalSpy spy(&engine, &XorEngine::finished);

    engine.runMultiByte(cipher, 2, 50);

    QCOMPARE(spy.count(), 1);
    QVector<Candidate> results = qvariant_cast<QVector<Candidate>>(spy.at(0).at(0));
    QVERIFY(!results.isEmpty());

    const Candidate &best = results.first();
    QCOMPARE(best.key, key);
    QCOMPARE(best.plaintext, plain);
}

void TestXorTool::testExtendedEncodings() {
    // Base32 ("MZXW6===" -> "foo")
    QCOMPARE(EncodingUtils::detectEncoding(QStringLiteral("MZXW6===")), EncodingType::Base32);
    QCOMPARE(EncodingUtils::decodeInput(QStringLiteral("MZXW6==="), EncodingType::Auto), QByteArray("foo"));

    // Adobe ASCII85 ("<~9jqo^~>" -> "Man ", "<~z~>" -> 4 zero bytes)
    QCOMPARE(EncodingUtils::detectEncoding(QStringLiteral("<~9jqo^~>")), EncodingType::Base85);
    QCOMPARE(EncodingUtils::decodeInput(QStringLiteral("<~9jqo^~>"), EncodingType::Auto), QByteArray("Man "));
    QCOMPARE(EncodingUtils::decodeInput(QStringLiteral("<~z~>"), EncodingType::Base85), QByteArray("\x00\x00\x00\x00", 4));

    // URL percent-encoding
    QCOMPARE(EncodingUtils::detectEncoding(QStringLiteral("FLAG%7Burl_dec0d3%7D")), EncodingType::Url);
    QCOMPARE(EncodingUtils::decodeInput(QStringLiteral("FLAG%7Burl_dec0d3%7D"), EncodingType::Auto), QByteArray("FLAG{url_dec0d3}"));

    // Reverse bytes
    QCOMPARE(EncodingUtils::decodeInput(QStringLiteral("}galf{GALF"), EncodingType::Raw, QByteArray(), true), QByteArray("FLAG{flag}"));
}

void TestXorTool::testAddSubRot() {
    // 1. Byte ADD (+5)
    QByteArray plainAdd = "FLAG{byte_add_sub_test}";
    QByteArray cipherAdd;
    for (char c : plainAdd) {
        cipherAdd.append(static_cast<char>((static_cast<uint8_t>(c) + 5) & 0xFF));
    }

    XorEngine engine;
    QSignalSpy spy(&engine, &XorEngine::finished);
    engine.runAddSubRot(cipherAdd, 20);
    QCOMPARE(spy.count(), 1);
    QVector<Candidate> results = qvariant_cast<QVector<Candidate>>(spy.at(0).at(0));
    QVERIFY(!results.isEmpty());
    QCOMPARE(results.first().plaintext, plainAdd);

    // 2. Caesar ROT-13
    QByteArray cipherRot = "SYNT{ebg13_grfg_synt}";
    spy.clear();
    engine.runAddSubRot(cipherRot, 20);
    QCOMPARE(spy.count(), 1);
    results = qvariant_cast<QVector<Candidate>>(spy.at(0).at(0));
    QVERIFY(!results.isEmpty());
    QCOMPARE(results.first().plaintext, QByteArray("FLAG{rot13_test_flag}"));
}

void TestXorTool::testBitwiseShiftNot() {
    // Encrypt with: ((b ^ 0x42) rotated left by 3) -> decrypt is ROR(cipher, 3) ^ 0x42
    QByteArray plain = "FLAG{bitwise_rol_xor_test}";
    QByteArray cipher;
    for (char ch : plain) {
        uint8_t b = static_cast<uint8_t>(ch) ^ 0x42;
        uint8_t enc = static_cast<uint8_t>((b << 3) | (b >> 5));
        cipher.append(static_cast<char>(enc));
    }

    XorEngine engine;
    QSignalSpy spy(&engine, &XorEngine::finished);
    engine.runBitwiseShiftNot(cipher, 20);
    QCOMPARE(spy.count(), 1);
    QVector<Candidate> results = qvariant_cast<QVector<Candidate>>(spy.at(0).at(0));
    QVERIFY(!results.isEmpty());
    QCOMPARE(results.first().plaintext, plain);
}

void TestXorTool::testComboXorAddRol() {
    // Encrypt with: ((b ^ 0x5A) + 0x17) & 0xFF -> decrypt is ((c - 0x17) & 0xFF) ^ 0x5A
    QByteArray plain = "FLAG{combo_xor_add_works}";
    QByteArray cipher;
    for (char ch : plain) {
        uint8_t enc = static_cast<uint8_t>(((static_cast<uint8_t>(ch) ^ 0x5A) + 0x17) & 0xFF);
        cipher.append(static_cast<char>(enc));
    }

    XorEngine engine;
    QSignalSpy spy(&engine, &XorEngine::finished);
    engine.runComboXorAddRol(cipher, true, true, 200);
    QCOMPARE(spy.count(), 1);
    QVector<Candidate> results = qvariant_cast<QVector<Candidate>>(spy.at(0).at(0));
    QVERIFY(!results.isEmpty());
    QCOMPARE(results.first().plaintext, plain);
}

QTEST_MAIN(TestXorTool)
#include "test_xortool.moc"
