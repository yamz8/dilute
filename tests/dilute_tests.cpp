#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "backend.h"
#include "calc.h"
#include "cli.h"
#include "filepicker.h"
#include "recipefile.h"
#include "recipemodel.h"
#include "theme.h"
#include "translator.h"
#include "units.h"

// Answers every dialog with a fixed path, as if the person picked it.
class FakePicker : public FilePicker {
public:
    QString answer;
    void open(const QString &, const QList<Filter> &, Done done) override {
        if (!answer.isEmpty())
            done(answer);
    }
    void save(const QString &, const QString &, const QList<Filter> &, Done done) override {
        if (!answer.isEmpty())
            done(answer);
    }
};

namespace {
bool near(double a, double b, double relative = 1e-9) {
    return std::fabs(a - b) <= relative * std::max(std::fabs(a), std::fabs(b));
}

Field given(double value, const char *unit) { return Field::given(value, unit); }

QMap<QString, Field> dilution(Field c1, Field v1, Field c2, Field v2) {
    return {{"c1", c1}, {"v1", v1}, {"c2", c2}, {"v2", v2}};
}

QVariantMap formField(const QString &text, const QString &unit) { return {{"text", text}, {"unit", unit}}; }

Backend::Paths tempPaths(const QTemporaryDir &dir) {
    return {dir.path() + "/config", dir.path() + "/data", dir.path() + "/state"};
}
} // namespace

class DiluteTests : public QObject {
    Q_OBJECT

private slots:
    void init() { QLocale::setDefault(QLocale::c()); }

    // Units

    void everyUnitRoundTripsThroughItsBase_data() {
        QTest::addColumn<QString>("symbol");
        for (const auto &unit : Units::all())
            QTest::newRow(qPrintable(unit.symbol)) << unit.symbol;
    }
    void everyUnitRoundTripsThroughItsBase() {
        QFETCH(QString, symbol);
        const Unit unit = Units::unit(symbol);
        QCOMPARE(unit.symbol, symbol);
        for (const auto &other : Units::all()) {
            if (other.dimension != unit.dimension)
                continue;
            QString error;
            const auto there = Units::convert({1.234, unit}, other, 0, &error);
            QVERIFY2(there, qPrintable(error));
            const auto back = Units::convert({*there, other}, unit, 0, &error);
            QVERIFY2(back, qPrintable(error));
            QVERIFY(near(*back, 1.234));
        }
    }

    void unitConversions_data() {
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("to");
        QTest::addColumn<double>("mw");
        QTest::addColumn<double>("expected");
        QTest::newRow("M to mM") << "1 M" << "mM" << 0.0 << 1000.0;
        QTest::newRow("mM to µM") << "1 mM" << "µM" << 0.0 << 1000.0;
        QTest::newRow("µM to nM") << "1 µM" << "nM" << 0.0 << 1000.0;
        QTest::newRow("nM to pM") << "1 nM" << "pM" << 0.0 << 1000.0;
        QTest::newRow("pM to M") << "1 pM" << "M" << 0.0 << 1e-12;
        QTest::newRow("g to mg") << "1 g" << "mg" << 0.0 << 1000.0;
        QTest::newRow("mg to µg") << "1 mg" << "µg" << 0.0 << 1000.0;
        QTest::newRow("µg to ng") << "1 µg" << "ng" << 0.0 << 1000.0;
        QTest::newRow("L to mL") << "1 L" << "mL" << 0.0 << 1000.0;
        QTest::newRow("mL to µL") << "1 mL" << "µL" << 0.0 << 1000.0;
        QTest::newRow("µL to L") << "1 µL" << "L" << 0.0 << 1e-6;
        QTest::newRow("w/v to mg/mL") << "1 %(w/v)" << "mg/mL" << 0.0 << 10.0;
        QTest::newRow("mg/mL to w/v") << "5 mg/mL" << "%(w/v)" << 0.0 << 0.5;
        QTest::newRow("mg/mL to g/L") << "2 mg/mL" << "g/L" << 0.0 << 2.0;
        QTest::newRow("ng/µL is µg/mL") << "50 ng/µL" << "µg/mL" << 0.0 << 50.0;
        QTest::newRow("v/v to v/v") << "70 %(v/v)" << "%(v/v)" << 0.0 << 70.0;
        QTest::newRow("X to X") << "10 X" << "X" << 0.0 << 10.0;
        QTest::newRow("mM to mg/mL with MW") << "150 mM" << "mg/mL" << 58.44 << 8.766;
        QTest::newRow("w/v to M with MW") << "0.9 %(w/v)" << "mM" << 58.44 << 154.0041067762;
        QTest::newRow("g to mmol with MW") << "5.844 g" << "mmol" << 58.44 << 100.0;
        QTest::newRow("µmol to mg with MW") << "100 µmol" << "mg" << 180.16 << 18.016;
        QTest::newRow("mol to g with MW") << "1 mol" << "g" << 121.14 << 121.14;
    }
    void unitConversions() {
        QFETCH(QString, from);
        QFETCH(QString, to);
        QFETCH(double, mw);
        QFETCH(double, expected);
        QString error;
        const auto quantity = Units::parse(from, &error);
        QVERIFY2(quantity, qPrintable(error));
        const auto value = Units::convert(*quantity, Units::unit(to), mw, &error);
        QVERIFY2(value, qPrintable(error));
        QVERIFY2(near(*value, expected, 1e-9), qPrintable(QString::number(*value, 'g', 15)));
    }

    void incompatibleUnitsAreErrorsNotNumbers_data() {
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("to");
        QTest::addColumn<double>("mw");
        QTest::newRow("molar to mass conc without MW") << "1 mM" << "mg/mL" << 0.0;
        QTest::newRow("molar to v/v") << "1 mM" << "%(v/v)" << 58.44;
        QTest::newRow("X to molar") << "10 X" << "M" << 58.44;
        QTest::newRow("w/v to v/v") << "1 %(w/v)" << "%(v/v)" << 0.0;
        QTest::newRow("volume to mass") << "1 mL" << "g" << 0.0;
        QTest::newRow("mass to molar") << "1 g" << "mM" << 58.44;
        QTest::newRow("mass to moles without MW") << "1 g" << "mol" << 0.0;
        QTest::newRow("concentration to volume") << "1 mM" << "mL" << 0.0;
    }
    void incompatibleUnitsAreErrorsNotNumbers() {
        QFETCH(QString, from);
        QFETCH(QString, to);
        QFETCH(double, mw);
        QString error;
        const auto value = Units::convert(*Units::parse(from, &error), Units::unit(to), mw, &error);
        QVERIFY(!value);
        QVERIFY(!error.isEmpty());
    }

    void parsesWhatPeopleType_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<double>("value");
        QTest::addColumn<QString>("unit");
        QTest::newRow("glued") << "10mM" << 10.0 << "mM";
        QTest::newRow("spaced") << "10 mM" << 10.0 << "mM";
        QTest::newRow("u for micro") << "100uL" << 100.0 << "µL";
        QTest::newRow("greek mu") << QString("5 %1M").arg(QChar(0x03BC)) << 5.0 << "µM";
        QTest::newRow("lowercase l") << "50ml" << 50.0 << "mL";
        QTest::newRow("exponent") << "1e-3M" << 1e-3 << "M";
        QTest::newRow("w/v without parens") << "5%w/v" << 5.0 << "%(w/v)";
        QTest::newRow("w/v spaced") << "5 % (w/v)" << 5.0 << "%(w/v)";
        QTest::newRow("v/v") << "70%v/v" << 70.0 << "%(v/v)";
        QTest::newRow("x stock") << "10x" << 10.0 << "X";
        QTest::newRow("ug/ml") << "2ug/ml" << 2.0 << "µg/mL";
        QTest::newRow("ng/ul") << "50ng/ul" << 50.0 << "ng/µL";
    }
    void parsesWhatPeopleType() {
        QFETCH(QString, text);
        QFETCH(double, value);
        QFETCH(QString, unit);
        QString error;
        const auto quantity = Units::parse(text, &error);
        QVERIFY2(quantity, qPrintable(error));
        QVERIFY(near(quantity->value, value));
        QCOMPARE(quantity->unit.symbol, unit);
    }

    void rejectsWhatIsntAQuantity_data() {
        QTest::addColumn<QString>("text");
        QTest::newRow("no number") << "mM";
        QTest::newRow("no unit") << "10";
        QTest::newRow("bare percent") << "5%";
        QTest::newRow("unknown unit") << "10 furlongs";
        QTest::newRow("empty") << "";
    }
    void rejectsWhatIsntAQuantity() {
        QFETCH(QString, text);
        QString error;
        QVERIFY(!Units::parse(text, &error));
        QVERIFY(!error.isEmpty());
    }

    void picksTheReadableUnit() {
        QCOMPARE(Units::best(0.0005, Dimension::Volume).symbol, QString("µL"));
        QCOMPARE(Units::best(0.05, Dimension::Volume).symbol, QString("mL"));
        QCOMPARE(Units::best(2, Dimension::Volume).symbol, QString("L"));
        QCOMPARE(Units::best(1e-10, Dimension::Volume).symbol, QString("µL"));
        QCOMPARE(Units::best(0.00099996, Dimension::Molar).symbol, QString("mM"));
        QCOMPARE(Units::best(3e-8, Dimension::Molar).symbol, QString("nM"));
        QCOMPARE(Units::bestLike(0.5, Units::unit("%(w/v)")).symbol, QString("%(w/v)"));
    }

    void formatsSignificantFigures() {
        QCOMPARE(Units::formatNumber(5, 3, QLocale::c()), QString("5.00"));
        QCOMPARE(Units::formatNumber(8.766, 3, QLocale::c()), QString("8.77"));
        QCOMPARE(Units::formatNumber(9.996, 3, QLocale::c()), QString("10.0"));
        QCOMPARE(Units::formatNumber(123456, 3, QLocale::c()), QString("123000"));
        QCOMPARE(Units::formatNumber(0.000123456, 2, QLocale::c()), QString("0.00012"));
        QCOMPARE(Units::formatNumber(1500, 4, QLocale::c()), QString("1500"));
        QCOMPARE(Units::formatNumber(0, 3, QLocale::c()), QString("0"));
        QCOMPARE(Units::formatCompact(10, 3, QLocale::c()), QString("10"));
        QCOMPARE(Units::formatCompact(9.98, 2, QLocale::c()), QString("10"));
    }

    void formatsAndParsesInTheLanguage() {
        const QLocale danish("da_DK"), german("de_DE");
        QCOMPARE(Units::formatNumber(1.5, 3, danish), QString("1,50"));
        QCOMPARE(Units::format({1500, Units::unit("µL")}, 4, german), QString("1500\u00A0µL"));
        QCOMPARE(Units::parseNumber("0,5", danish).value(), 0.5);
        // C still works where the language uses a comma.
        QCOMPARE(Units::parseNumber("0.5", danish).value(), 0.5);
        QCOMPARE(Units::parse("2,5 mL", nullptr, german)->value, 2.5);
        QVERIFY(!Units::parseNumber("abc", german));
    }

    // C1V1 = C2V2

    void solvesEachUnknown_data() {
        QTest::addColumn<QString>("unknown");
        QTest::addColumn<double>("expected");
        QTest::addColumn<QString>("unit");
        QTest::newRow("c1") << "c1" << 10.0 << "mM";
        QTest::newRow("v1") << "v1" << 5.0 << "mL";
        QTest::newRow("c2") << "c2" << 1.0 << "mM";
        QTest::newRow("v2") << "v2" << 50.0 << "mL";
    }
    void solvesEachUnknown() {
        QFETCH(QString, unknown);
        QFETCH(double, expected);
        QFETCH(QString, unit);
        QMap<QString, Field> fields = dilution(given(10, "mM"), given(5, "mL"), given(1, "mM"), given(50, "mL"));
        fields[unknown] = Field::asked(unit);
        const Answer answer = solveDilution(fields, {});
        QVERIFY2(answer.ok, qPrintable(answer.error));
        QCOMPARE(answer.solved, QStringList{unknown});
        QCOMPARE(answer.values[unknown].unit.symbol, unit);
        QVERIFY(near(answer.values[unknown].value, expected));
        QVERIFY(near(answer.values["diluent"].base(), 0.045));
        QVERIFY(answer.warnings.isEmpty());
    }

    void answersInTheAskedUnit() {
        const Answer answer = solveDilution(
            dilution(given(10, "mM"), Field::asked("µL"), given(1, "mM"), given(50, "mL")), {});
        QVERIFY(answer.ok);
        QCOMPARE(answer.values["v1"].unit.symbol, QString("µL"));
        QVERIFY(near(answer.values["v1"].value, 5000));
    }

    void dilutesAnXStock() {
        const Answer answer = solveDilution(dilution(given(10, "X"), {}, given(1, "X"), given(500, "mL")), {});
        QVERIFY(answer.ok);
        QVERIFY(near(answer.values["v1"].base(), 0.05));
        QVERIFY(near(answer.values["diluent"].base(), 0.45));
    }

    void dilutionNeedsThreeValues() {
        const Answer answer = solveDilution(dilution(given(10, "mM"), {}, given(1, "mM"), {}), {});
        QVERIFY(!answer.ok);
        QVERIFY(answer.incomplete);
    }

    void dilutionRefusesMixedConcentrations() {
        const Answer answer =
            solveDilution(dilution(given(10, "mM"), {}, given(1, "mg/mL"), given(50, "mL")), {});
        QVERIFY(!answer.ok);
        QVERIFY(!answer.incomplete);
        QVERIFY(answer.values.isEmpty());
        // An unknown asked for in another kind of concentration is no better.
        QVERIFY(!solveDilution(dilution(given(10, "mM"), given(5, "mL"), Field::asked("%(v/v)"), given(50, "mL")), {}).ok);
        // Nor is a volume where a concentration belongs.
        QVERIFY(!solveDilution(dilution(given(10, "mL"), {}, given(1, "mM"), given(50, "mL")), {}).ok);
    }

    void dilutionCantConcentrate() {
        QVERIFY(!solveDilution(dilution(given(1, "mM"), {}, given(10, "mM"), given(50, "mL")), {}).ok);
        QVERIFY(!solveDilution(dilution(given(10, "mM"), given(60, "mL"), {}, given(50, "mL")), {}).ok);
        QVERIFY(!solveDilution(dilution(given(0, "mM"), {}, given(1, "mM"), given(50, "mL")), {}).ok);
    }

    void dilutionChecksFourValuesAgree() {
        QVERIFY(solveDilution(dilution(given(10, "mM"), given(5, "mL"), given(1, "mM"), given(50, "mL")), {}).ok);
        QVERIFY(!solveDilution(dilution(given(10, "mM"), given(6, "mL"), given(1, "mM"), given(50, "mL")), {}).ok);
    }

    void flagsVolumesTooSmallToPipette() {
        Settings settings;
        const Answer small = solveDilution(dilution(given(1, "M"), {}, given(1, "mM"), given(200, "µL")), settings);
        QVERIFY(small.ok);
        QCOMPARE(small.warnings.size(), 1); // 0.2 µL of stock
        const Answer edge = solveDilution(dilution(given(10, "mM"), {}, given(1, "mM"), given(5, "µL")), settings);
        QVERIFY(edge.warnings.isEmpty()); // exactly 0.5 µL is fine
        settings.minimumPipetteLiters = 2e-6;
        QCOMPARE(solveDilution(dilution(given(10, "mM"), {}, given(1, "mM"), given(5, "µL")), settings).warnings.size(), 1);
    }

    // Molarity

    void solvesMolarity_data() {
        QTest::addColumn<QString>("unknown");
        QTest::addColumn<double>("expectedBase");
        QTest::newRow("mass") << "mass" << 8.766;
        QTest::newRow("concentration") << "concentration" << 0.15;
        QTest::newRow("volume") << "volume" << 1.0;
    }
    void solvesMolarity() {
        QFETCH(QString, unknown);
        QFETCH(double, expectedBase);
        QMap<QString, Field> fields{{"mass", given(8.766, "g")}, {"concentration", given(150, "mM")},
                                    {"volume", given(1, "L")}};
        fields[unknown] = Field::asked(unknown == "mass" ? "g" : unknown == "volume" ? "L" : "mM");
        const Answer answer = solveMolarity(fields, 58.44, {});
        QVERIFY2(answer.ok, qPrintable(answer.error));
        QVERIFY(answer.solved.contains(unknown));
        QVERIFY(near(answer.values[unknown].base(), expectedBase, 1e-9));
        QVERIFY(near(answer.values["moles"].base(), 0.15, 1e-9));
    }

    void molarityFromMoles() {
        const Answer answer = solveMolarity({{"moles", given(75, "mmol")}, {"volume", given(500, "mL")}}, 58.44, {});
        QVERIFY(answer.ok);
        QVERIFY(near(answer.values["concentration"].base(), 0.15));
        QVERIFY(near(answer.values["mass"].base(), 4.383));
    }

    void molarityInMassConcentrationNeedsNoWeight() {
        const Answer answer =
            solveMolarity({{"concentration", given(5, "mg/mL")}, {"volume", given(20, "mL")}}, std::nullopt, {});
        QVERIFY2(answer.ok, qPrintable(answer.error));
        QVERIFY(near(answer.values["mass"].base(), 0.1));
        QVERIFY(!answer.values.contains("moles"));
    }

    void molarityWorksOutTheWeight() {
        const Answer answer = solveMolarity(
            {{"mass", given(5.844, "g")}, {"moles", given(100, "mmol")}, {"volume", given(1, "L")}}, std::nullopt, {});
        QVERIFY(answer.ok);
        QVERIFY(answer.solved.contains("mw"));
        QVERIFY(near(answer.values["mw"].value, 58.44));
    }

    void molarityErrors() {
        // Grams and molar without a weight can't meet.
        const Answer noWeight =
            solveMolarity({{"concentration", given(150, "mM")}, {"volume", given(1, "L")}}, std::nullopt, {});
        QVERIFY(!noWeight.ok);
        QVERIFY(noWeight.incomplete);
        QVERIFY(!solveMolarity({{"volume", given(1, "L")}}, 58.44, {}).ok);
        QVERIFY(!solveMolarity({{"mass", given(1, "mL")}, {"volume", given(1, "L")}}, 58.44, {}).ok);
        QVERIFY(!solveMolarity({{"concentration", given(1, "X")}, {"volume", given(1, "L")}}, 58.44, {}).ok);
        QVERIFY(!solveMolarity({{"concentration", given(1, "mM")}, {"volume", given(1, "L")}}, -3, {}).ok);
        const Answer contradiction = solveMolarity(
            {{"mass", given(10, "g")}, {"concentration", given(150, "mM")}, {"volume", given(1, "L")}}, 58.44, {});
        QVERIFY(!contradiction.ok);
        QVERIFY(!contradiction.incomplete);
    }

    // Serial dilution

    void plansASerialDilution() {
        const SerialPlan plan = planSerial({1, Units::unit("mM")}, 10, 6, {100, Units::unit("µL")}, {});
        QVERIFY(plan.answer.ok);
        QCOMPARE(plan.tubes.size(), 6);
        const QStringList expected{"100 µM", "10.0 µM", "1.00 µM", "100 nM", "10.0 nM", "1.00 nM"};
        for (int i = 0; i < 6; ++i) {
            QCOMPARE(Units::format(plan.tubes[i].concentration, 3, QLocale::c()), expected[i]);
            QVERIFY(near(plan.tubes[i].diluent.base(), 900e-6));
            QVERIFY(near(plan.tubes[i].transfer.base(), 100e-6));
            QVERIFY(near(plan.tubes[i].fold, std::pow(10, i + 1)));
        }
        QVERIFY(near(plan.tubes[0].finalVolume.base(), 900e-6));
        QVERIFY(near(plan.tubes[5].finalVolume.base(), 1e-3));
        QVERIFY(near(plan.totalDiluent.base(), 5.4e-3));
        QVERIFY(near(plan.stockNeeded.base(), 100e-6));
    }

    void serialKeepsNonMolarUnits() {
        const SerialPlan plan = planSerial({2, Units::unit("%(w/v)")}, 2, 3, {50, Units::unit("µL")}, {});
        QVERIFY(plan.answer.ok);
        QCOMPARE(plan.tubes[2].concentration.unit.symbol, QString("%(w/v)"));
        QVERIFY(near(plan.tubes[2].concentration.value, 0.25));
        QVERIFY(near(plan.tubes[0].diluent.base(), 50e-6));
    }

    void serialErrorsAndWarnings() {
        const Quantity start{1, Units::unit("mM")}, transfer{100, Units::unit("µL")};
        QVERIFY(!planSerial(start, 1, 6, transfer, {}).answer.ok);
        QVERIFY(!planSerial(start, 10, 0, transfer, {}).answer.ok);
        QVERIFY(!planSerial({1, Units::unit("mL")}, 10, 6, transfer, {}).answer.ok);
        QVERIFY(!planSerial(start, 10, 6, {1, Units::unit("mM")}, {}).answer.ok);
        const SerialPlan tiny = planSerial(start, 10, 3, {0.2, Units::unit("µL")}, {});
        QVERIFY(tiny.answer.ok);
        QCOMPARE(tiny.answer.warnings.size(), 1);
    }

    // Buffer recipes

    void presetPbsAmounts() {
        const Recipe pbs = *Presets::find("pbs");
        const RecipePlan plan = planRecipe(pbs, {});
        QVERIFY2(plan.answer.ok, qPrintable(plan.answer.error));
        QVERIFY(near(plan.amounts[0].amount.base(), 0.137 * 58.44));
        QVERIFY(!plan.amounts[0].isVolume);
        QVERIFY(near(plan.water.base(), 1));
    }

    void everyPresetWorksOut() {
        for (const auto &name : Presets::names()) {
            const RecipePlan plan = planRecipe(*Presets::find(name), {});
            QVERIFY2(plan.answer.ok, qPrintable(name + ": " + plan.answer.error));
        }
        QCOMPARE(Presets::find("tae")->name, QString("TAE 50X"));
        QVERIFY(!Presets::find("nonsense"));
    }

    void scalesRecipes() {
        const Recipe ripa = *Presets::find("RIPA");
        const RecipePlan full = planRecipe(ripa, {});
        const RecipePlan half = planRecipe(scaled(ripa, {50, Units::unit("mL")}), {});
        QVERIFY(half.answer.ok);
        for (int i = 0; i < ripa.components.size(); ++i)
            QVERIFY(near(half.amounts[i].amount.base() * 2, full.amounts[i].amount.base()));
        // Stocks are pipetted, solids weighed, neat liquids pipetted.
        QVERIFY(full.amounts[0].isVolume);  // Tris-HCl from 1 M
        QVERIFY(near(full.amounts[0].amount.base(), 5e-3));
        QVERIFY(full.amounts[2].isVolume);  // NP-40 neat
        QVERIFY(near(full.amounts[2].amount.base(), 1e-3));
        QVERIFY(!full.amounts[3].isVolume); // deoxycholate weighed
        QVERIFY(near(full.amounts[3].amount.base(), 0.5));
        QVERIFY(near(full.amounts[4].amount.base(), 1e-3)); // SDS from 10 %
        QVERIFY(near(full.water.base(), 0.1 - 5e-3 - 3e-3 - 1e-3 - 1e-3 - 0.2e-3));
    }

    void recipeComponentErrors() {
        Recipe recipe;
        recipe.volume = {100, Units::unit("mL")};
        recipe.components = {
            {"No weight", 0, {10, Units::unit("mM")}, {}},
            {"Weak stock", 0, {10, Units::unit("mM")}, Quantity{1, Units::unit("mM")}},
            {"X without stock", 0, {1, Units::unit("X")}, {}},
            {"Molar from w/v stock without MW", 0, {10, Units::unit("mM")}, Quantity{10, Units::unit("%(w/v)")}},
            {"Molar from w/v stock with MW", 58.44, {100, Units::unit("mM")}, Quantity{10, Units::unit("%(w/v)")}},
        };
        const RecipePlan plan = planRecipe(recipe, {});
        QVERIFY(!plan.answer.ok);
        for (int i = 0; i < 4; ++i) {
            QVERIFY2(!plan.amounts[i].ok, qPrintable(recipe.components[i].name));
            QVERIFY(!plan.amounts[i].error.isEmpty());
        }
        QVERIFY(plan.amounts[4].ok);
        // 100 mM NaCl is 5.844 g/L; 10 % is 100 g/L.
        QVERIFY(near(plan.amounts[4].amount.base(), 0.1 * 5.844 / 100));
    }

    void recipeLiquidsCantOverflow() {
        Recipe recipe;
        recipe.volume = {10, Units::unit("mL")};
        recipe.components = {{"Glycerol", 0, {60, Units::unit("%(v/v)")}, {}},
                             {"Ethanol", 0, {50, Units::unit("%(v/v)")}, {}}};
        QVERIFY(!planRecipe(recipe, {}).answer.ok);
    }

    void recipeFlagsTinyVolumes() {
        Recipe recipe;
        // 1 mM from a 1 M stock in 100 µL is 0.1 µL.
        recipe.volume = {100, Units::unit("µL")};
        recipe.components = {{"DTT", 154.25, {1, Units::unit("mM")}, Quantity{1, Units::unit("M")}}};
        const RecipePlan plan = planRecipe(recipe, {});
        QVERIFY(plan.answer.ok);
        QVERIFY(!plan.amounts[0].warning.isEmpty());
        QCOMPARE(plan.answer.warnings.size(), 1);
    }

    void recipeFileRoundTrips() {
        QTemporaryDir dir;
        const QString path = dir.filePath("ripa.dilute");
        const Recipe ripa = *Presets::find("RIPA");
        QString error;
        QVERIFY2(RecipeFile::save(path, ripa, &error), qPrintable(error));
        Recipe loaded;
        QVERIFY2(RecipeFile::load(path, &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.name, ripa.name);
        QCOMPARE(loaded.notes, ripa.notes);
        QCOMPARE(loaded.components.size(), ripa.components.size());
        for (int i = 0; i < ripa.components.size(); ++i) {
            QCOMPARE(loaded.components[i].name, ripa.components[i].name);
            QCOMPARE(loaded.components[i].final.value, ripa.components[i].final.value);
            QCOMPARE(loaded.components[i].final.unit.symbol, ripa.components[i].final.unit.symbol);
            QCOMPARE(loaded.components[i].stock.has_value(), ripa.components[i].stock.has_value());
            QCOMPARE(loaded.components[i].mw, ripa.components[i].mw);
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(json.value("format").toString(), QString("dilute"));
        QCOMPARE(json.value("volume").toString(), QString("100 mL"));
    }

    void recipeFileRejectsNonsense() {
        Recipe recipe;
        QString error;
        QVERIFY(!RecipeFile::fromJson({{"format", "other"}}, &recipe, &error));
        QVERIFY(!RecipeFile::fromJson({{"format", "dilute"}, {"volume", "1 mM"}}, &recipe, &error));
        QVERIFY(!RecipeFile::fromJson(QJsonDocument::fromJson("{\"format\": \"dilute\", \"volume\": \"1 L\", "
                                                                "\"components\": [{\"name\": \"X\", \"final\": \"10 parsecs\"}]}")
                                          .object(), &recipe, &error));
        QVERIFY(error.contains("parsecs"));
    }

    // Backend, as the window uses it

    void backendFormatsInTheLanguage() {
        QTemporaryDir dir;
        QLocale::setDefault(QLocale("da_DK"));
        Backend backend(nullptr, tempPaths(dir));
        const QVariantMap result = backend.dilution({{"c1", formField("10", "mM")}, {"v1", formField("", "mL")},
                                                     {"c2", formField("0,5", "mM")}, {"v2", formField("50", "mL")}});
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(result.value("main").toString(), QString("2,50\u00A0mL"));
        QCOMPARE(result.value("values").toMap().value("v1").toMap().value("number").toString(), QString("2,50"));
        // The exact form for files stays in C.
        QCOMPARE(result.value("values").toMap().value("v1").toMap().value("serialized").toString(), QString("2.5 mL"));
        QVERIFY(!result.value("card").toMap().isEmpty());
    }

    void backendRejectsTextThatIsntANumber() {
        QTemporaryDir dir;
        Backend backend(nullptr, tempPaths(dir));
        const QVariantMap result = backend.dilution({{"c1", formField("ten", "mM")}, {"c2", formField("1", "mM")},
                                                     {"v2", formField("50", "mL")}});
        QVERIFY(!result.value("ok").toBool());
        QVERIFY(result.value("error").toString().contains("ten"));
    }

    void backendSerialAndMolarity() {
        QTemporaryDir dir;
        Backend backend(nullptr, tempPaths(dir));
        const QVariantMap serial = backend.serial({{"start", formField("1", "mM")}, {"factor", "1:10"}, {"steps", 4},
                                                   {"transfer", formField("100", "µL")}});
        QVERIFY(serial.value("ok").toBool());
        QCOMPARE(serial.value("rows").toList().size(), 4);
        QCOMPARE(serial.value("rows").toList()[1].toMap().value("fold").toString(), QString("1:100"));
        QCOMPARE(serial.value("main").toString().split('\n').size(), 5);
        const QVariantMap molarity = backend.molarity({{"mw", "58.44"}, {"reagent", "NaCl"},
                                                       {"mass", formField("", "g")},
                                                       {"concentration", formField("150", "mM")},
                                                       {"volume", formField("1", "L")}});
        QVERIFY(molarity.value("ok").toBool());
        QCOMPARE(molarity.value("main").toString(), QString("8.77 g"));
    }

    void settingsAndReagentsPersist() {
        QTemporaryDir dir;
        {
            Backend backend(nullptr, tempPaths(dir));
            QCOMPARE(backend.significantFigures(), 3);
            backend.setSignificantFigures(4);
            backend.setMinimumPipette(1);
            backend.setReagent(0, "Sodium chloride", "58.443");
            backend.addReagent();
        }
        Backend backend(nullptr, tempPaths(dir));
        QCOMPARE(backend.significantFigures(), 4);
        QCOMPARE(backend.minimumPipette(), 1.0);
        QCOMPARE(backend.reagents().first().toMap().value("name").toString(), QString("Sodium chloride"));
        QCOMPARE(backend.reagents().first().toMap().value("mw").toDouble(), 58.443);
        QCOMPARE(backend.reagents().size(), builtInReagents().size() + 1);
        backend.resetReagents();
        QCOMPARE(backend.reagents().size(), builtInReagents().size());
    }

    void historyKeepsRecentCalculations() {
        QTemporaryDir dir;
        Backend backend(nullptr, tempPaths(dir));
        backend.remember(0, {}, "first");
        backend.remember(0, {}, "first, still typing");
        QCOMPARE(backend.history().size(), 1);
        backend.remember(2, {}, "another calculator");
        QCOMPARE(backend.history().size(), 2);
        backend.remember(2, {}, "another calculator");
        QCOMPARE(backend.history().size(), 2);
        Backend reopened(nullptr, tempPaths(dir));
        QCOMPARE(reopened.history().size(), 2);
        QCOMPARE(reopened.history().first().toMap().value("summary").toString(), QString("another calculator"));
        reopened.clearHistory();
        QVERIFY(reopened.history().isEmpty());
    }

    void exportsTheCard() {
        QTemporaryDir dir;
        FakePicker picker;
        Backend backend(&picker, tempPaths(dir));
        const QVariantMap card = backend.dilution({{"c1", formField("10", "mM")}, {"c2", formField("1", "mM")},
                                                   {"v2", formField("50", "mL")}}).value("card").toMap();
        picker.answer = dir.filePath("card");
        backend.exportCard(card, "pdf");
        QFile pdf(dir.filePath("card.pdf"));
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QVERIFY(pdf.readAll().startsWith("%PDF"));
        picker.answer = dir.filePath("card.svg");
        backend.exportCard(card, "svg");
        QFile svg(dir.filePath("card.svg"));
        QVERIFY(svg.open(QIODevice::ReadOnly));
        const QByteArray text = svg.readAll();
        QVERIFY(text.contains("<svg"));
        QVERIFY(text.contains("Diluent"));
    }

    void recipeModelEditsUndoesAndSaves() {
        QTemporaryDir dir;
        FakePicker picker;
        Backend backend(&picker, tempPaths(dir));
        RecipeModel model(&backend);
        QVERIFY(!model.dirty());
        model.loadPreset("PBS");
        QCOMPARE(model.rowCount(), 4);
        QVERIFY(!model.dirty());
        QVERIFY(model.result().value("ok").toBool());
        QCOMPARE(model.data(model.index(0), RecipeModel::AmountRole).toString(), QString("8.01 g"));

        model.setVolume("500", "mL");
        QVERIFY(model.dirty());
        QCOMPARE(model.data(model.index(0), RecipeModel::AmountRole).toString(), QString("4.00 g"));
        model.setField(0, "finalText", "abc");
        QVERIFY(!model.data(model.index(0), RecipeModel::ErrorRole).toString().isEmpty());
        QVERIFY(!model.result().value("ok").toBool());
        model.undo();
        QVERIFY(model.result().value("ok").toBool());
        model.undo();
        QCOMPARE(model.volumeText(), QString("1"));
        model.redo();
        QCOMPARE(model.volumeText(), QString("500"));

        model.addComponent();
        QCOMPARE(model.rowCount(), 5);
        // A blank row is no error; it's waiting to be filled in.
        QVERIFY(model.result().value("ok").toBool());
        model.removeComponent(4);

        picker.answer = dir.filePath("my pbs");
        model.save();
        QVERIFY(!model.dirty());
        QCOMPARE(model.fileName(), QString("my pbs.dilute"));
        Recipe loaded;
        QVERIFY(RecipeFile::load(dir.filePath("my pbs.dilute"), &loaded, nullptr));
        QCOMPARE(loaded.volume.value, 500.0);

        model.setName("My PBS");
        model.savePreset();
        QVERIFY(model.presets().contains("My PBS"));
        model.newRecipe();
        QCOMPARE(model.rowCount(), 1);
        model.loadPreset("My PBS");
        QCOMPARE(model.volumeText(), QString("500"));
    }

    void recipeModelRefusesToSaveErrors() {
        QTemporaryDir dir;
        FakePicker picker;
        Backend backend(&picker, tempPaths(dir));
        RecipeModel model(&backend);
        model.loadPreset("TE");
        model.setField(0, "stockText", "one");
        picker.answer = dir.filePath("broken.dilute");
        QSignalSpy messages(&backend, &Backend::message);
        model.save();
        QVERIFY(!QFile::exists(dir.filePath("broken.dilute")));
        QCOMPARE(messages.size(), 1);
        QVERIFY(messages.first().at(1).toBool());
    }

    // Command line

    void commandLineExitCodes_data() {
        QTest::addColumn<QStringList>("arguments");
        QTest::addColumn<int>("code");
        QTest::newRow("c1v1") << QStringList{"c1v1", "--c1", "10mM", "--c2", "1mM", "--v2", "50mL", "--json"} << 0;
        QTest::newRow("mass") << QStringList{"mass", "--mw", "58.44", "--conc", "150mM", "--vol", "1L", "--json"} << 0;
        QTest::newRow("serial") << QStringList{"serial", "--start", "1mM", "--factor", "10", "--steps", "6", "--vol", "100uL", "--json"} << 0;
        QTest::newRow("recipe") << QStringList{"recipe", "pbs", "--vol", "500mL", "--json"} << 0;
        QTest::newRow("convert") << QStringList{"convert", "150mM", "--to", "mg/mL", "--mw", "58.44", "--json"} << 0;
        QTest::newRow("bad unit") << QStringList{"c1v1", "--c1", "10mQ", "--c2", "1mM", "--v2", "50mL"} << 1;
        QTest::newRow("mixed units") << QStringList{"c1v1", "--c1", "10mM", "--c2", "1mg/mL", "--v2", "50mL"} << 1;
        QTest::newRow("volume as concentration") << QStringList{"c1v1", "--c1", "10mL", "--c2", "1mM", "--v2", "50mL"} << 1;
        QTest::newRow("recipe volume in mM") << QStringList{"recipe", "pbs", "--vol", "500mM"} << 1;
        QTest::newRow("unknown preset") << QStringList{"recipe", "lysogeny"} << 1;
        QTest::newRow("convert without MW") << QStringList{"convert", "150mM", "--to", "mg/mL"} << 1;
    }
    void commandLineExitCodes() {
        QFETCH(QStringList, arguments);
        QFETCH(int, code);
        QCOMPARE(runCli(QStringList{"dilute"} + arguments), code);
    }

    // Theme and language

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", "{\"Save\": \"Guardar\", \"Open\": \"Abrir\"}");
        write(dir.path() + "/pt-BR.json", "{\"Save\": \"Salvar\"}");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", "{\"%n clips\": {\"one\": \"%n clip\", \"other\": \"%n clips\"}}");
        write(dir.path() + "/ru.json",
              "{\"%n clips\": {\"one\": \"%n клип\", \"few\": \"%n клипа\", \"many\": \"%n клипов\", "
              "\"other\": \"%n клипа\"}}");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

    // Every string the window or the calculators show is in every catalog,
    // so switching language never leaves English behind.
    void catalogsCoverEveryString() {
        // qsTr("…"), tr("…") and translate("dilute", "…"). Built from a plain
        // string, since moc can't read a raw string full of quotes.
        const QRegularExpression pattern(
            "(?:qsTr|\\btr|translate\\(\"dilute\",)\\s*\\(?\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
        QSet<QString> strings;
        QDirIterator files(SRCDIR, {"*.qml", "*.cpp"}, QDir::Files);
        while (files.hasNext()) {
            const QString path = files.next();
            if (path.endsWith("cli.cpp") || path.endsWith("main.cpp"))
                continue; // English for scripts, by design
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            auto matches = pattern.globalMatch(QString::fromUtf8(file.readAll()));
            while (matches.hasNext())
                strings << matches.next().captured(1).replace("\\\"", "\"");
        }
        QVERIFY(strings.size() > 100);
        const QJsonObject english = catalog("en");
        for (const QString code : {"da", "de"}) {
            const QJsonObject translations = catalog(code);
            QStringList missing;
            for (const auto &string : strings)
                if (!translations.contains(string))
                    missing << string;
            missing.sort();
            QVERIFY2(missing.isEmpty(), qPrintable(code + " is missing:\n" + missing.join('\n')));
        }
        for (const auto &string : strings)
            if (string.contains("%n"))
                QVERIFY2(english.value(string).isObject(), qPrintable("en.json needs plural forms for " + string));
    }

private:
    static void write(const QString &path, const QByteArray &contents) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
    static QJsonObject catalog(const QString &code) {
        QFile file(QString(SRCDIR) + "/i18n/" + code + ".json");
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }
};

QTEST_MAIN(DiluteTests)
#include "dilute_tests.moc"
