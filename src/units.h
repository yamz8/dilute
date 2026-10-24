#pragma once

#include <QLocale>
#include <QString>
#include <QStringList>
#include <optional>

// What a unit measures. Concentrations come in four kinds that only convert
// into each other where chemistry allows: molar and mass concentration through
// a molecular weight, the other two not at all.
enum class Dimension { Molar, MassConcentration, VolumeFraction, Stock, Mass, Amount, Volume };

struct Unit {
    QString symbol;
    Dimension dimension = Dimension::Volume;
    // Multiplies a value in this unit into the dimension's base unit:
    // M, g/L, fraction, X, g, mol and L.
    double factor = 1;

    bool operator==(const Unit &other) const { return symbol == other.symbol; }
};

struct Quantity {
    double value = 0;
    Unit unit;

    double base() const { return value * unit.factor; }
    static Quantity fromBase(double base, const Unit &unit) { return {base / unit.factor, unit}; }
};

namespace Units {

const QList<Unit> &all();
// Accepts the spellings people type: u or μ for µ, l for L, "%w/v",
// "% (w/v)", "x" for X.
std::optional<Unit> find(const QString &symbol);
Unit unit(const QString &symbol);
QStringList symbols(Dimension dimension);
QStringList concentrationSymbols();

bool isConcentration(Dimension dimension);
QString dimensionName(Dimension dimension);

// The unit that puts a value (in base units) between 1 and 1000, from the
// dimension's everyday ladder: 0.0005 L reads as 500 µL.
Unit best(double base, Dimension dimension);
// best() when the unit is on its dimension's ladder; otherwise the unit as
// given, so a %(w/v) series stays in %(w/v).
Unit bestLike(double base, const Unit &unit);

// Converts a concentration between kinds. Molar and mass concentration meet
// through mw (g/mol); anything else across kinds is an error, never a guess.
std::optional<double> convertConcentration(double base, Dimension from, Dimension to, double mw, QString *error);
std::optional<double> convert(const Quantity &quantity, const Unit &to, double mw, QString *error);

// "10mM", "10 mM", "1e-3 M", "0,5 µL" in the given locale (C always works).
std::optional<Quantity> parse(const QString &text, QString *error, const QLocale &locale = QLocale::c());
std::optional<double> parseNumber(const QString &text, const QLocale &locale = QLocale::c());

// Rounds to significant figures and keeps the trailing zeros that say how
// precise the number is: 5 at three figures is 5.00.
QString formatNumber(double value, int significantFigures, const QLocale &locale = QLocale());
// Rounded to significant figures but without padding zeros: a dilution of
// 1:10 isn't 1:10.0.
QString formatCompact(double value, int significantFigures, const QLocale &locale = QLocale());
// Strips floating point noise (4.4999999999 to 4.5) for machine output.
double clean(double value);
QString format(const Quantity &quantity, int significantFigures, const QLocale &locale = QLocale());
// In the dimension's best unit.
QString formatBase(double base, Dimension dimension, int significantFigures, const QLocale &locale = QLocale());
// Round-trips exactly, for files and the command line.
QString serialize(const Quantity &quantity);

} // namespace Units
