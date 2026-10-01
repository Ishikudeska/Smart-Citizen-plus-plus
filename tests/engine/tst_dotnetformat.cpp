#include "engine/forge/DotNetFormat.h"

#include <QTest>

#include <cmath>
#include <limits>

using namespace engine::forge;

class TestDotNetFormat : public QObject
{
    Q_OBJECT

private slots:
    void singles_data()
    {
        QTest::addColumn<float>("value");
        QTest::addColumn<QString>("expected");
        // Samples from unforge's real output first.
        QTest::newRow("plain") << 1.67f << "1.67";
        QTest::newRow("small exponent") << 1e-05f << "1E-05";
        QTest::newRow("negative small") << -1.000196e-05f << "-1.000196E-05";
        QTest::newRow("large exponent") << 1.093429e9f << "1.093429E+09";
        QTest::newRow("padded integer") << 161410900.0f << "161410900";
        QTest::newRow("negative zero") << -0.0f << "-0";
        QTest::newRow("zero") << 0.0f << "0";
        QTest::newRow("1e-4 stays fixed") << 0.0001f << "0.0001";
        QTest::newRow("hundred") << 100.0f << "100";
        QTest::newRow("nine digits") << 123456789.0f << "123456790";
        QTest::newRow("1e9") << 1e9f << "1E+09";
        QTest::newRow("tenth") << 0.1f << "0.1";
        QTest::newRow("third") << 1.0f / 3.0f << "0.33333334";
        QTest::newRow("nan") << std::numeric_limits<float>::quiet_NaN() << "NaN";
        QTest::newRow("inf") << std::numeric_limits<float>::infinity() << "Infinity";
        QTest::newRow("-inf") << -std::numeric_limits<float>::infinity() << "-Infinity";
        QTest::newRow("max") << std::numeric_limits<float>::max() << "3.4028235E+38";
    }

    void singles()
    {
        QFETCH(float, value);
        QFETCH(QString, expected);
        QCOMPARE(QString::fromStdString(formatSingle(value)), expected);
    }

    void doubles_data()
    {
        QTest::addColumn<double>("value");
        QTest::addColumn<QString>("expected");
        QTest::newRow("tenth") << 0.1 << "0.1";
        QTest::newRow("seventeen digits") << 1e16 << "10000000000000000";
        QTest::newRow("1e17") << 1e17 << "1E+17";
        QTest::newRow("small") << 1.5e-5 << "1.5E-05";
        QTest::newRow("third") << 1.0 / 3.0 << "0.3333333333333333";
        QTest::newRow("negative") << -2.5 << "-2.5";
        QTest::newRow("big exponent") << 1e300 << "1E+300";
    }

    void doubles()
    {
        QFETCH(double, value);
        QFETCH(QString, expected);
        QCOMPARE(QString::fromStdString(formatDouble(value)), expected);
    }

    // System.Guid(a, b, c, d..k) from DataForge's byte order.
    void guidByteOrder()
    {
        std::uint8_t bytes[16];
        for (int i = 0; i < 16; ++i)
            bytes[i] = static_cast<std::uint8_t>(i);
        QCOMPARE(formatGuid(bytes), std::string("07060504-0302-0100-0f0e-0d0c0b0a0908"));
    }
};

QTEST_APPLESS_MAIN(TestDotNetFormat)
#include "tst_dotnetformat.moc"
