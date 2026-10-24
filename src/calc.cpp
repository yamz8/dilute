#include "calc.h"

#include <QCoreApplication>
#include <cmath>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("dilute", text); }

// Relative tolerance for "these agree", far below any significant figure
// anyone shows but above floating point noise.
constexpr double tolerance = 1e-9;

bool agrees(double a, double b) { return std::fabs(a - b) <= tolerance * std::max(std::fabs(a), std::fabs(b)); }

QString formatted(const Quantity &quantity, const Settings &settings) {
    return Units::format(quantity, settings.significantFigures);
}

Quantity volume(double liters) { return Quantity::fromBase(liters, Units::best(liters, Dimension::Volume)); }

Answer failure(const QString &error) {
    Answer answer;
    answer.error = error;
    return answer;
}

Answer incomplete(const QString &hint) {
    Answer answer;
    answer.incomplete = true;
    answer.error = hint;
    return answer;
}

// A volume someone has to pipette, checked against the smallest they can.
void checkPipette(Answer &answer, const Quantity &amount, const QString &what, const Settings &settings) {
    const double liters = amount.base();
    if (liters > 0 && liters < settings.minimumPipetteLiters * (1 - tolerance))
        answer.warnings << tr("%1 of %2 is below the %3 you can pipette")
                               .arg(formatted(amount, settings), what,
                                    formatted(volume(settings.minimumPipetteLiters), settings));
}
} // namespace

Answer solveDilution(const QMap<QString, Field> &fields, const Settings &settings) {
    const QStringList keys{"c1", "v1", "c2", "v2"};
    const QMap<QString, QString> labels{{"c1", "C1"}, {"v1", "V1"}, {"c2", "C2"}, {"v2", "V2"}};
    int given = 0;
    for (const auto &key : keys)
        given += fields.value(key).isGiven();
    if (given < 3)
        return incomplete(tr("Fill in three of the four values; the empty one is worked out"));

    for (const auto &key : keys) {
        const Field field = fields.value(key);
        if (!field.isGiven())
            continue;
        if (!field.unit)
            return failure(tr("%1 needs a unit").arg(labels[key]));
        if (*field.value <= 0)
            return failure(tr("%1 must be more than zero").arg(labels[key]));
        const bool concentration = key.startsWith('c');
        if (concentration && !Units::isConcentration(field.unit->dimension))
            return failure(tr("%1 must be a concentration, not %2").arg(labels[key], field.unit->symbol));
        if (!concentration && field.unit->dimension != Dimension::Volume)
            return failure(tr("%1 must be a volume, not %2").arg(labels[key], field.unit->symbol));
    }

    // The two concentrations must be the same kind, given or asked for: a
    // molar stock diluted to mg/mL needs a molecular weight this calculator
    // doesn't ask for, so it says so rather than guess.
    const Field c1 = fields.value("c1"), c2 = fields.value("c2");
    if (c1.unit && c2.unit && c1.unit->dimension != c2.unit->dimension)
        return failure(tr("C1 (%1) and C2 (%2) are different kinds of concentration")
                           .arg(c1.unit->symbol, c2.unit->symbol));

    QMap<QString, double> base;
    for (const auto &key : keys)
        if (fields.value(key).isGiven())
            base[key] = fields.value(key).quantity()->base();

    Answer answer;
    QString unknown;
    for (const auto &key : keys)
        if (!fields.value(key).isGiven())
            unknown = key;

    if (unknown.isEmpty()) {
        if (!agrees(base["c1"] * base["v1"], base["c2"] * base["v2"]))
            return failure(tr("The four values don't agree: C1 × V1 isn't C2 × V2. Empty one to work it out"));
    } else if (unknown == "c1") {
        base["c1"] = base["c2"] * base["v2"] / base["v1"];
    } else if (unknown == "v1") {
        base["v1"] = base["c2"] * base["v2"] / base["c1"];
    } else if (unknown == "c2") {
        base["c2"] = base["c1"] * base["v1"] / base["v2"];
    } else {
        base["v2"] = base["c1"] * base["v1"] / base["c2"];
    }

    if (base["c2"] > base["c1"] && !agrees(base["c2"], base["c1"]))
        return failure(tr("The final concentration is above the stock's; diluting can't raise it"));
    if (base["v1"] > base["v2"] && !agrees(base["v1"], base["v2"]))
        return failure(tr("The stock volume is more than the final volume"));

    for (const auto &key : keys) {
        const Field field = fields.value(key);
        if (field.isGiven()) {
            answer.values[key] = *field.quantity();
            continue;
        }
        Unit unit;
        if (field.unit)
            unit = *field.unit;
        else if (key.startsWith('c'))
            unit = Units::bestLike(base[key], (key == "c1" ? c2 : c1).unit.value());
        else
            unit = Units::best(base[key], Dimension::Volume);
        answer.values[key] = Quantity::fromBase(base[key], unit);
        answer.solved << key;
    }

    const double diluent = std::max(0.0, base["v2"] - base["v1"]);
    answer.values["diluent"] = agrees(base["v2"], base["v1"]) ? Quantity{0, Units::unit("mL")} : volume(diluent);
    checkPipette(answer, volume(base["v1"]), tr("stock"), settings);
    if (!agrees(base["v2"], base["v1"]))
        checkPipette(answer, answer.values["diluent"], tr("diluent"), settings);
    answer.ok = true;
    return answer;
}

Answer solveMolarity(const QMap<QString, Field> &fields, std::optional<double> mw, const Settings &settings) {
    const QMap<QString, Dimension> expected{{"mass", Dimension::Mass},
                                            {"moles", Dimension::Amount},
                                            {"volume", Dimension::Volume}};
    const QMap<QString, QString> labels{
        {"mass", tr("Mass")}, {"moles", tr("Moles")}, {"concentration", tr("Concentration")}, {"volume", tr("Volume")}};

    for (const auto &key : labels.keys()) {
        const Field field = fields.value(key);
        if (field.isGiven() && !field.unit)
            return failure(tr("%1 needs a unit").arg(labels[key]));
        if (field.isGiven() && *field.value <= 0)
            return failure(tr("%1 must be more than zero").arg(labels[key]));
        if (!field.unit)
            continue;
        const Dimension dimension = field.unit->dimension;
        if (key == "concentration") {
            if (dimension != Dimension::Molar && dimension != Dimension::MassConcentration)
                return failure(tr("Use a molar or mass concentration here, not %1").arg(field.unit->symbol));
        } else if (dimension != expected[key]) {
            return failure(tr("%1 can't be in %2").arg(labels[key], field.unit->symbol));
        }
    }
    if (mw && *mw <= 0)
        return failure(tr("The molecular weight must be more than zero"));

    // Grams, moles, mol/L, g/L, liters and g/mol, tied by four products.
    std::optional<double> m, n, cM, cW, V, weight = mw;
    auto take = [&](const QString &key) -> std::optional<double> {
        const Field field = fields.value(key);
        return field.isGiven() ? std::optional<double>(field.quantity()->base()) : std::nullopt;
    };
    m = take("mass");
    n = take("moles");
    V = take("volume");
    const Field concentration = fields.value("concentration");
    if (concentration.isGiven())
        (concentration.unit->dimension == Dimension::Molar ? cM : cW) = concentration.quantity()->base();

    struct Product {
        std::optional<double> *x, *y, *z;
    };
    const QList<Product> products{{&m, &n, &weight}, {&n, &cM, &V}, {&cW, &cM, &weight}, {&m, &cW, &V}};
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto &p : products) {
            if (!*p.x && *p.y && *p.z) {
                *p.x = **p.y * **p.z;
                changed = true;
            } else if (*p.x && !*p.y && *p.z && **p.z != 0) {
                *p.y = **p.x / **p.z;
                changed = true;
            } else if (*p.x && *p.y && !*p.z && **p.y != 0) {
                *p.z = **p.x / **p.y;
                changed = true;
            }
        }
    }
    for (const auto &p : products)
        if (*p.x && *p.y && *p.z && !agrees(**p.x, **p.y * **p.z) &&
            std::fabs(**p.x - **p.y * **p.z) > 1e-6 * std::fabs(**p.x))
            return failure(tr("These values contradict each other; empty the one to work out"));

    const Dimension concentrationDimension =
        concentration.unit ? concentration.unit->dimension : (cM ? Dimension::Molar : Dimension::MassConcentration);
    const std::optional<double> c = concentrationDimension == Dimension::Molar ? cM : cW;
    if (!V || (!m && !n) || (!cM && !cW))
        return incomplete(tr("Fill in all but one of mass, concentration and volume"));
    if (!m)
        return incomplete(tr("Add the molecular weight to work out the mass"));
    if (!c)
        return incomplete(tr("Add the molecular weight to answer in %1")
                              .arg(concentration.unit ? concentration.unit->symbol : QStringLiteral("M")));

    Answer answer;
    auto put = [&](const QString &key, double base, Dimension dimension) {
        const Field field = fields.value(key);
        if (field.isGiven()) {
            answer.values[key] = *field.quantity();
            return;
        }
        const Unit unit = field.unit ? *field.unit : Units::best(base, dimension);
        answer.values[key] = Quantity::fromBase(base, unit);
        answer.solved << key;
    };
    put("mass", *m, Dimension::Mass);
    put("concentration", *c, concentrationDimension);
    put("volume", *V, Dimension::Volume);
    if (n)
        put("moles", *n, Dimension::Amount);
    if (weight) {
        answer.values["mw"] = Quantity{*weight, Unit{"g/mol", Dimension::Mass, 1}};
        if (!mw)
            answer.solved << "mw";
    }
    if (answer.solved.contains("volume"))
        checkPipette(answer, answer.values["volume"], tr("solution"), settings);
    answer.ok = true;
    return answer;
}

SerialPlan planSerial(const Quantity &start, double factor, int steps, const Quantity &transfer,
                      const Settings &settings) {
    SerialPlan plan;
    if (!Units::isConcentration(start.unit.dimension)) {
        plan.answer = failure(tr("The start must be a concentration, not %1").arg(start.unit.symbol));
        return plan;
    }
    if (transfer.unit.dimension != Dimension::Volume) {
        plan.answer = failure(tr("The transfer must be a volume, not %1").arg(transfer.unit.symbol));
        return plan;
    }
    if (start.value <= 0 || transfer.value <= 0) {
        plan.answer = failure(tr("The start concentration and transfer volume must be more than zero"));
        return plan;
    }
    if (!(factor > 1)) {
        plan.answer = failure(tr("The dilution factor must be more than 1"));
        return plan;
    }
    if (steps < 1 || steps > 96) {
        plan.answer = failure(tr("Use between 1 and 96 tubes"));
        return plan;
    }

    // Each tube starts with (factor − 1) transfers of diluent, so adding one
    // transfer makes it 1:factor. Every tube but the last passes a transfer
    // on; the last keeps it (or discards it to match the others).
    const double moved = transfer.base();
    const double diluent = moved * (factor - 1);
    for (int i = 1; i <= steps; ++i) {
        SerialTube tube;
        tube.index = i;
        tube.fold = std::pow(factor, i);
        const double c = start.base() / tube.fold;
        tube.concentration = Quantity::fromBase(c, Units::bestLike(c, start.unit));
        tube.diluent = volume(diluent);
        tube.transfer = volume(moved);
        tube.finalVolume = volume(i < steps ? diluent : diluent + moved);
        plan.tubes << tube;
    }
    plan.stockNeeded = volume(moved);
    plan.totalDiluent = volume(diluent * steps);
    plan.answer.ok = true;
    checkPipette(plan.answer, volume(moved), tr("transfer"), settings);
    checkPipette(plan.answer, volume(diluent), tr("diluent"), settings);
    return plan;
}

RecipePlan planRecipe(const Recipe &recipe, const Settings &settings) {
    RecipePlan plan;
    if (recipe.volume.unit.dimension != Dimension::Volume || recipe.volume.value <= 0) {
        plan.answer = failure(tr("The final volume must be a volume more than zero"));
        return plan;
    }
    if (recipe.components.isEmpty()) {
        plan.answer = incomplete(tr("Add a component"));
        return plan;
    }

    const double total = recipe.volume.base();
    double liquids = 0;
    bool allOk = true;
    for (const auto &component : recipe.components) {
        ComponentAmount amount;
        const Dimension kind = component.final.unit.dimension;
        if (!Units::isConcentration(kind)) {
            amount.error = tr("%1 isn't a concentration").arg(component.final.unit.symbol);
        } else if (component.final.value <= 0) {
            amount.error = tr("The final concentration must be more than zero");
        } else if (component.stock) {
            QString error;
            const auto final = Units::convertConcentration(component.final.base(), kind,
                                                           component.stock->unit.dimension, component.mw, &error);
            if (!final) {
                amount.error = error;
            } else if (component.stock->value <= 0) {
                amount.error = tr("The stock concentration must be more than zero");
            } else if (*final > component.stock->base() && !agrees(*final, component.stock->base())) {
                amount.error = tr("The stock is weaker than the final concentration");
            } else {
                amount.ok = true;
                amount.isVolume = true;
                amount.amount = volume(*final / component.stock->base() * total);
            }
        } else if (kind == Dimension::Molar) {
            if (component.mw > 0) {
                const double grams = component.final.base() * total * component.mw;
                amount.ok = true;
                amount.amount = Quantity::fromBase(grams, Units::best(grams, Dimension::Mass));
            } else {
                amount.error = tr("Add the molecular weight or a stock");
            }
        } else if (kind == Dimension::MassConcentration) {
            const double grams = component.final.base() * total;
            amount.ok = true;
            amount.amount = Quantity::fromBase(grams, Units::best(grams, Dimension::Mass));
        } else if (kind == Dimension::VolumeFraction) {
            // A liquid added neat, such as glycerol or a detergent.
            amount.ok = true;
            amount.isVolume = true;
            amount.amount = volume(component.final.base() * total);
        } else {
            amount.error = tr("An X concentration needs a stock, such as 10X");
        }

        if (amount.ok && amount.isVolume) {
            liquids += amount.amount.base();
            if (amount.amount.base() < settings.minimumPipetteLiters * (1 - tolerance))
                amount.warning = tr("Too little to pipette; make an intermediate stock");
        }
        if (!amount.warning.isEmpty())
            plan.answer.warnings << component.name + ": " + amount.warning;
        allOk = allOk && amount.ok;
        plan.amounts << amount;
    }

    if (liquids > total && !agrees(liquids, total)) {
        plan.answer.error = tr("The liquids add up to %1, more than the final volume")
                                .arg(formatted(volume(liquids), settings));
        return plan;
    }
    plan.water = volume(std::max(0.0, total - liquids));
    plan.answer.ok = allOk;
    if (!allOk)
        plan.answer.error = tr("Fix the components marked in red");
    return plan;
}

Recipe scaled(Recipe recipe, const Quantity &volume) {
    // Amounts follow from the concentrations and the volume, so scaling is
    // just a new volume.
    recipe.volume = volume;
    return recipe;
}

namespace {
Quantity q(double value, const char *symbol) { return {value, Units::unit(symbol)}; }

QList<Recipe> presets() {
    return {
        {"PBS", tr("1X, pH 7.4. Dissolve in 80% of the volume, adjust the pH with HCl, then bring to volume."),
         q(1, "L"),
         {{"NaCl", 58.44, q(137, "mM"), {}},
          {"KCl", 74.55, q(2.7, "mM"), {}},
          {"Na2HPO4", 141.96, q(10, "mM"), {}},
          {"KH2PO4", 136.09, q(1.8, "mM"), {}}}},
        {"TAE 50X", tr("Dilute 1:50 for 1X: 40 mM Tris, 20 mM acetate, 1 mM EDTA."), q(1, "L"),
         {{"Tris base", 121.14, q(2, "M"), {}},
          {"Glacial acetic acid", 60.05, q(5.71, "%(v/v)"), {}},
          {"EDTA pH 8.0", 372.24, q(50, "mM"), q(0.5, "M")}}},
        {"TBE 10X", tr("Dilute 1:10 for 1X: 89 mM Tris, 89 mM borate, 2 mM EDTA."), q(1, "L"),
         {{"Tris base", 121.14, q(890, "mM"), {}},
          {"Boric acid", 61.83, q(890, "mM"), {}},
          {"EDTA pH 8.0", 372.24, q(20, "mM"), q(0.5, "M")}}},
        {"TE", tr("1X, pH 8.0."), q(100, "mL"),
         {{"Tris-HCl pH 8.0", 157.60, q(10, "mM"), q(1, "M")},
          {"EDTA pH 8.0", 372.24, q(1, "mM"), q(0.5, "M")}}},
        {"Tris-HCl 1 M", tr("Dissolve in 80% of the volume, adjust to pH 8.0 with concentrated HCl, then bring to volume."),
         q(1, "L"),
         {{"Tris base", 121.14, q(1, "M"), {}}}},
        {"RIPA", tr("Add protease and phosphatase inhibitors just before use. Keep on ice."), q(100, "mL"),
         {{"Tris-HCl pH 8.0", 157.60, q(50, "mM"), q(1, "M")},
          {"NaCl", 58.44, q(150, "mM"), q(5, "M")},
          {"NP-40", 0, q(1, "%(v/v)"), {}},
          {"Sodium deoxycholate", 414.55, q(0.5, "%(w/v)"), {}},
          {"SDS", 288.38, q(0.1, "%(w/v)"), q(10, "%(w/v)")},
          {"EDTA pH 8.0", 372.24, q(1, "mM"), q(0.5, "M")}}},
    };
}
} // namespace

namespace Presets {
QStringList names() {
    QStringList list;
    for (const auto &recipe : presets())
        list << recipe.name;
    return list;
}

// Matches the whole name or its first word, any case: "tae" is TAE 50X.
std::optional<Recipe> find(const QString &name) {
    for (const auto &recipe : presets())
        if (recipe.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return recipe;
    for (const auto &recipe : presets())
        if (recipe.name.section(' ', 0, 0).compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return recipe;
    return std::nullopt;
}
} // namespace Presets

QList<Reagent> builtInReagents() {
    return {
        {"NaCl", 58.44},
        {"KCl", 74.55},
        {"Tris base", 121.14},
        {"Tris-HCl", 157.60},
        {"EDTA disodium dihydrate", 372.24},
        {"Glucose", 180.16},
        {"Sucrose", 342.30},
        {"MgCl2·6H2O", 203.30},
        {"MgSO4·7H2O", 246.47},
        {"CaCl2·2H2O", 147.01},
        {"Na2HPO4", 141.96},
        {"KH2PO4", 136.09},
        {"NaOH", 40.00},
        {"HEPES", 238.30},
        {"Glycine", 75.07},
        {"Boric acid", 61.83},
        {"SDS", 288.38},
        {"DTT", 154.25},
        {"Imidazole", 68.08},
    };
}
