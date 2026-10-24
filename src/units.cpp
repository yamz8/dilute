#include "units.h"

#include <QCoreApplication>
#include <QHash>
#include <cmath>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("dilute", text); }

const QString micro = QStringLiteral("µ");

// Strips what people put around the same unit: spaces, parentheses, case of
// the w/v suffix, and the Greek mu that looks like the micro sign.
QString normalized(QString symbol) {
    symbol = symbol.trimmed();
    symbol.replace(QChar(0x03BC), micro);
    if (symbol.startsWith('%')) {
        symbol.remove(' ').remove('(').remove(')');
        return symbol.toLower();
    }
    symbol.remove(' ');
    if (symbol.startsWith('u'))
        symbol.replace(0, 1, micro);
    symbol.replace("/u", "/" + micro);
    return symbol;
}

double roundSignificant(double value, int figures) {
    if (value == 0 || !std::isfinite(value))
        return value;
    const double scale = std::pow(10.0, figures - 1 - std::floor(std::log10(std::fabs(value))));
    return std::round(value * scale) / scale;
}

QList<QString> ladder(Dimension dimension) {
    switch (dimension) {
    case Dimension::Molar: return {"M", "mM", "µM", "nM", "pM"};
    case Dimension::MassConcentration: return {"mg/mL", "µg/mL"};
    case Dimension::Mass: return {"g", "mg", "µg", "ng"};
    case Dimension::Amount: return {"mol", "mmol", "µmol", "nmol", "pmol"};
    case Dimension::Volume: return {"L", "mL", "µL"};
    case Dimension::VolumeFraction: return {"%(v/v)"};
    case Dimension::Stock: return {"X"};
    }
    return {};
}
} // namespace

namespace Units {

const QList<Unit> &all() {
    static const QList<Unit> units = {
        {"M", Dimension::Molar, 1},
        {"mM", Dimension::Molar, 1e-3},
        {"µM", Dimension::Molar, 1e-6},
        {"nM", Dimension::Molar, 1e-9},
        {"pM", Dimension::Molar, 1e-12},
        {"g/L", Dimension::MassConcentration, 1},
        {"mg/mL", Dimension::MassConcentration, 1},
        {"µg/mL", Dimension::MassConcentration, 1e-3},
        {"ng/µL", Dimension::MassConcentration, 1e-3},
        // 1 % (w/v) is 1 g per 100 mL.
        {"%(w/v)", Dimension::MassConcentration, 10},
        {"%(v/v)", Dimension::VolumeFraction, 1e-2},
        {"X", Dimension::Stock, 1},
        {"g", Dimension::Mass, 1},
        {"mg", Dimension::Mass, 1e-3},
        {"µg", Dimension::Mass, 1e-6},
        {"ng", Dimension::Mass, 1e-9},
        {"mol", Dimension::Amount, 1},
        {"mmol", Dimension::Amount, 1e-3},
        {"µmol", Dimension::Amount, 1e-6},
        {"nmol", Dimension::Amount, 1e-9},
        {"pmol", Dimension::Amount, 1e-12},
        {"L", Dimension::Volume, 1},
        {"mL", Dimension::Volume, 1e-3},
        {"µL", Dimension::Volume, 1e-6},
    };
    return units;
}

std::optional<Unit> find(const QString &symbol) {
    const QString wanted = normalized(symbol);
    if (wanted.isEmpty())
        return std::nullopt;
    for (const auto &unit : all())
        if (normalized(unit.symbol) == wanted)
            return unit;
    // No two units differ only by case, so "ml" and "mm" can only mean mL and
    // mM.
    for (const auto &unit : all())
        if (normalized(unit.symbol).compare(wanted, Qt::CaseInsensitive) == 0)
            return unit;
    return std::nullopt;
}

Unit unit(const QString &symbol) { return find(symbol).value_or(Unit{}); }

QStringList symbols(Dimension dimension) {
    QStringList list;
    for (const auto &unit : all())
        if (unit.dimension == dimension)
            list << unit.symbol;
    return list;
}

QStringList concentrationSymbols() {
    QStringList list;
    for (const auto &unit : all())
        if (isConcentration(unit.dimension))
            list << unit.symbol;
    return list;
}

bool isConcentration(Dimension dimension) {
    return dimension == Dimension::Molar || dimension == Dimension::MassConcentration ||
           dimension == Dimension::VolumeFraction || dimension == Dimension::Stock;
}

QString dimensionName(Dimension dimension) {
    switch (dimension) {
    case Dimension::Molar: return tr("molar concentration");
    case Dimension::MassConcentration: return tr("mass concentration");
    case Dimension::VolumeFraction: return tr("volume fraction");
    case Dimension::Stock: return tr("X stock");
    case Dimension::Mass: return tr("mass");
    case Dimension::Amount: return tr("amount");
    case Dimension::Volume: return tr("volume");
    }
    return {};
}

Unit best(double base, Dimension dimension) {
    const auto steps = ladder(dimension);
    for (const auto &symbol : steps) {
        const Unit candidate = unit(symbol);
        // Compared after rounding, so 0.99996 mM shows as 1.00 mM rather
        // than 1000 µM.
        if (roundSignificant(std::fabs(base) / candidate.factor, 4) >= 1)
            return candidate;
    }
    return unit(steps.last());
}

Unit bestLike(double base, const Unit &original) {
    return ladder(original.dimension).contains(original.symbol) ? best(base, original.dimension) : original;
}

std::optional<double> convertConcentration(double base, Dimension from, Dimension to, double mw, QString *error) {
    if (from == to)
        return base;
    const bool molarAndMass = (from == Dimension::Molar && to == Dimension::MassConcentration) ||
                              (from == Dimension::MassConcentration && to == Dimension::Molar);
    if (!molarAndMass) {
        if (error)
            *error = tr("A %1 can't be converted to a %2").arg(dimensionName(from), dimensionName(to));
        return std::nullopt;
    }
    if (!(mw > 0)) {
        if (error)
            *error = tr("Converting between %1 and %2 needs the molecular weight")
                         .arg(dimensionName(from), dimensionName(to));
        return std::nullopt;
    }
    return from == Dimension::Molar ? base * mw : base / mw;
}

std::optional<double> convert(const Quantity &quantity, const Unit &to, double mw, QString *error) {
    if (quantity.unit.dimension == to.dimension)
        return quantity.base() / to.factor;
    if (isConcentration(quantity.unit.dimension) && isConcentration(to.dimension)) {
        const auto base = convertConcentration(quantity.base(), quantity.unit.dimension, to.dimension, mw, error);
        if (!base)
            return std::nullopt;
        return *base / to.factor;
    }
    if ((quantity.unit.dimension == Dimension::Mass && to.dimension == Dimension::Amount) ||
        (quantity.unit.dimension == Dimension::Amount && to.dimension == Dimension::Mass)) {
        if (!(mw > 0)) {
            if (error)
                *error = tr("Converting between %1 and %2 needs the molecular weight")
                             .arg(dimensionName(quantity.unit.dimension), dimensionName(to.dimension));
            return std::nullopt;
        }
        const double base = quantity.unit.dimension == Dimension::Mass ? quantity.base() / mw : quantity.base() * mw;
        return base / to.factor;
    }
    if (error)
        *error = tr("%1 and %2 measure different things").arg(quantity.unit.symbol, to.symbol);
    return std::nullopt;
}

std::optional<double> parseNumber(const QString &text, const QLocale &locale) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return std::nullopt;
    bool ok = false;
    double value = locale.toDouble(trimmed, &ok);
    if (!ok)
        value = QLocale::c().toDouble(trimmed, &ok);
    if (!ok || !std::isfinite(value))
        return std::nullopt;
    return value;
}

std::optional<Quantity> parse(const QString &text, QString *error, const QLocale &locale) {
    const QString trimmed = text.trimmed();
    // The number runs until the first character that can't be part of one; no
    // unit starts with e, so 1e-3M splits as 1e-3 and M.
    const QString numberCharacters = QStringLiteral("0123456789.,+-eE '") + locale.decimalPoint() +
                                     locale.groupSeparator() + QChar(0x00A0) + QChar(0x202F);
    int split = 0;
    while (split < trimmed.size() && numberCharacters.contains(trimmed[split]))
        ++split;
    const QString number = trimmed.left(split).trimmed();
    const QString symbol = trimmed.mid(split).trimmed();
    const auto value = parseNumber(number, locale);
    if (!value) {
        if (error)
            *error = tr("“%1” doesn't start with a number").arg(trimmed);
        return std::nullopt;
    }
    if (symbol.isEmpty()) {
        if (error)
            *error = tr("“%1” needs a unit").arg(trimmed);
        return std::nullopt;
    }
    if (normalized(symbol) == "%") {
        if (error)
            *error = tr("Say %(w/v) or %(v/v) rather than %");
        return std::nullopt;
    }
    const auto found = find(symbol);
    if (!found) {
        if (error)
            *error = tr("Unknown unit “%1”").arg(symbol);
        return std::nullopt;
    }
    return Quantity{*value, *found};
}

QString formatNumber(double value, int figures, const QLocale &locale) {
    if (!std::isfinite(value))
        return QStringLiteral("–");
    if (value == 0)
        return locale.toString(0);
    const double rounded = roundSignificant(value, figures);
    const int magnitude = static_cast<int>(std::floor(std::log10(std::fabs(rounded))));
    const int decimals = std::max(0, figures - 1 - magnitude);
    QLocale plain = locale;
    // Group separators make 1500 read like a decimal in half the languages.
    plain.setNumberOptions(QLocale::OmitGroupSeparator);
    return plain.toString(rounded, 'f', decimals);
}

QString format(const Quantity &quantity, int figures, const QLocale &locale) {
    return formatNumber(quantity.value, figures, locale) + QChar(' ') + quantity.unit.symbol;
}

QString formatBase(double base, Dimension dimension, int figures, const QLocale &locale) {
    return format(Quantity::fromBase(base, best(base, dimension)), figures, locale);
}

QString serialize(const Quantity &quantity) {
    return QString::number(quantity.value, 'g', 15) + QChar(' ') + quantity.unit.symbol;
}

} // namespace Units
