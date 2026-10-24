#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <optional>

#include "units.h"

struct Settings {
    int significantFigures = 3;
    // Volumes below this can't be pipetted reliably and are flagged.
    double minimumPipetteLiters = 0.5e-6;
};

// One input of a calculator. A field left empty is the one to work out; its
// unit, when set, is the unit to answer in.
struct Field {
    std::optional<double> value;
    std::optional<Unit> unit;

    static Field given(double value, const QString &symbol) { return {value, Units::unit(symbol)}; }
    static Field asked(const QString &symbol = {}) {
        return symbol.isEmpty() ? Field{} : Field{std::nullopt, Units::unit(symbol)};
    }
    bool isGiven() const { return value.has_value(); }
    std::optional<Quantity> quantity() const {
        return value && unit ? std::optional<Quantity>(Quantity{*value, *unit}) : std::nullopt;
    }
};

// Every calculator answers with quantities in the units to show them in, the
// fields it worked out, and what's wrong. An incomplete form isn't an error:
// it has a hint and no answer yet.
struct Answer {
    bool ok = false;
    bool incomplete = false;
    QString error;
    QStringList warnings;
    QMap<QString, Quantity> values;
    QStringList solved;
};

// C1V1 = C2V2. Keys c1, v1, c2, v2, and diluent in the answer.
Answer solveDilution(const QMap<QString, Field> &fields, const Settings &settings);

// mass = moles × MW and moles = concentration × volume. Keys mass, moles,
// concentration, volume and mw (a plain number, in g/mol).
Answer solveMolarity(const QMap<QString, Field> &fields, std::optional<double> mw, const Settings &settings);

struct SerialTube {
    int index = 0;
    Quantity concentration;
    double fold = 1;
    Quantity diluent;
    Quantity transfer;
    Quantity finalVolume;
};

struct SerialPlan {
    Answer answer;
    QList<SerialTube> tubes;
    Quantity stockNeeded;
    Quantity totalDiluent;
};

// Each tube is filled with diluent and gets the transfer volume from the
// tube before it (the first from the stock), so tube i holds start / factor^i.
SerialPlan planSerial(const Quantity &start, double factor, int steps, const Quantity &transfer,
                      const Settings &settings);

struct Component {
    QString name;
    double mw = 0;
    Quantity final;
    std::optional<Quantity> stock;
};

struct Recipe {
    QString name;
    QString notes;
    Quantity volume{1, Units::unit("L")};
    QList<Component> components;
};

struct ComponentAmount {
    bool ok = false;
    bool isVolume = false;
    Quantity amount;
    QString error;
    QString warning;
};

struct RecipePlan {
    Answer answer;
    QList<ComponentAmount> amounts;
    // Water to bring the solution to volume, after the liquids.
    Quantity water;
};

RecipePlan planRecipe(const Recipe &recipe, const Settings &settings);
Recipe scaled(Recipe recipe, const Quantity &volume);

namespace Presets {
QStringList names();
std::optional<Recipe> find(const QString &name);
} // namespace Presets

struct Reagent {
    QString name;
    double mw = 0;
};

QList<Reagent> builtInReagents();
