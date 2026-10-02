#pragma once

#include <QString>
#include <QStringView>

#include <optional>

// Python number semantics the enhancements generator's text depends on:
// float()/int() parsing, repr() of floats, round(), and the format specs
// the generator uses ("{:,}", "{:,.1f}", "{:+.0f}", ...).
namespace core::py {

// float(s): surrounding whitespace allowed, "inf"/"nan" accepted, digit
// groups may use single underscores. Nothing for anything else.
std::optional<double> toFloat(QStringView s);
// int(s) for a base-10 string.
std::optional<qint64> toInt(QStringView s);

// repr(float): shortest round-trip digits, "1.0", "1e+16", "1.5e-05".
QString repr(double v);

// round(v): to the nearest integer, ties to even.
double round(double v);

// format(v, ".{decimals}f") with optional "," grouping and "+" sign.
QString fixed(double v, int decimals, bool thousands = false, bool plus = false);
// format(n, ",") / str(n) for an int.
QString integer(qint64 n, bool thousands = false);
// format(v, ",") for a float: repr() with the integer digits grouped.
QString groupedRepr(double v);

// "1234567" -> "1,234,567": digits before any '.', after any sign.
QString groupThousands(const QString &number);

} // namespace core::py
