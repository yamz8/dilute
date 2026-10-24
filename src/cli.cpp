#include "cli.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cstdio>

#include "calc.h"
#include "recipefile.h"

namespace {
void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
void print(const QJsonObject &object) { fprintf(stdout, "%s", QJsonDocument(object).toJson().constData()); }
int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

struct Command {
    QCommandLineParser parser;
    Command(const QString &name, const QString &description) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        parser.addPositionalArgument(name, description, name);
        parser.addOption({"json", "Print the result as JSON"});
        parser.addOption({"sig", "Significant figures in the text (default: 3)", "n", "3"});
        parser.addOption({"min-pipette", "Smallest volume you can pipette (default: 0.5uL)", "volume", "0.5uL"});
    }
    bool isJson() const { return parser.isSet("json"); }

    // Reads --sig and --min-pipette; false (after printing why) on nonsense.
    bool settings(Settings *settings) const {
        bool ok = false;
        settings->significantFigures = parser.value("sig").toInt(&ok);
        if (!ok || settings->significantFigures < 1 || settings->significantFigures > 8) {
            print(stderr, "--sig must be a whole number from 1 to 8.");
            return false;
        }
        QString error;
        const auto minimum = Units::parse(parser.value("min-pipette"), &error);
        if (!minimum || minimum->unit.dimension != Dimension::Volume) {
            print(stderr, "--min-pipette: " + (minimum ? minimum->unit.symbol + " isn't a volume" : error));
            return false;
        }
        settings->minimumPipetteLiters = minimum->base();
        return true;
    }

    // A quantity option such as --c1 10mM; unset is the unknown.
    bool field(const QString &option, Field *field) const {
        if (!parser.isSet(option))
            return true;
        QString error;
        const auto quantity = Units::parse(parser.value(option), &error);
        if (!quantity) {
            print(stderr, "--" + option + ": " + error);
            return false;
        }
        *field = Field::given(quantity->value, quantity->unit.symbol);
        return true;
    }
};

QJsonObject json(const Quantity &quantity, int figures) {
    return {{"value", quantity.value},
            {"unit", quantity.unit.symbol},
            {"text", Units::format(quantity, figures)}};
}

QJsonObject json(const Answer &answer, int figures) {
    QJsonObject values;
    for (auto it = answer.values.begin(); it != answer.values.end(); ++it)
        values.insert(it.key(), json(it.value(), figures));
    return {{"values", values},
            {"solved", QJsonArray::fromStringList(answer.solved)},
            {"warnings", QJsonArray::fromStringList(answer.warnings)}};
}

// The unit to answer in, from --in, checked against what the unknown is.
bool answerUnit(const Command &command, std::optional<Unit> *unit) {
    if (!command.parser.isSet("in"))
        return true;
    *unit = Units::find(command.parser.value("in"));
    if (!*unit) {
        print(stderr, "--in: unknown unit “" + command.parser.value("in") + "”");
        return false;
    }
    return true;
}

void warn(const Answer &answer) {
    for (const auto &warning : answer.warnings)
        print(stderr, "warning: " + warning);
}

int c1v1(const QStringList &arguments) {
    Command command("c1v1", "Solve C1V1 = C2V2: give three of --c1, --v1, --c2, --v2 and get the fourth, "
                            "plus the diluent to add.");
    command.parser.addOption({"c1", "Stock concentration, such as 10mM", "conc"});
    command.parser.addOption({"v1", "Volume of stock", "volume"});
    command.parser.addOption({"c2", "Final concentration", "conc"});
    command.parser.addOption({"v2", "Final volume", "volume"});
    command.parser.addOption({"in", "Unit to answer in", "unit"});
    command.parser.process(arguments);
    Settings settings;
    if (!command.settings(&settings))
        return 1;
    QMap<QString, Field> fields;
    for (const QString key : {"c1", "v1", "c2", "v2"})
        if (!command.field(key, &fields[key]))
            return 1;
    std::optional<Unit> unit;
    if (!answerUnit(command, &unit))
        return 1;
    if (unit)
        for (const QString key : {"c1", "v1", "c2", "v2"})
            if (!fields[key].isGiven())
                fields[key].unit = unit;

    const Answer answer = solveDilution(fields, settings);
    if (!answer.ok)
        return fail(answer.error);
    const int figures = settings.significantFigures;
    auto text = [&](const QString &key) { return Units::format(answer.values[key], figures); };
    if (command.isJson()) {
        print(json(answer, figures));
        return 0;
    }
    warn(answer);
    for (const auto &key : answer.solved)
        print(stdout, key.toUpper() + " = " + text(key));
    print(stdout, QString("Add %1 of stock (%2) to %3 of diluent for %4 at %5.")
                      .arg(text("v1"), text("c1"), text("diluent"), text("v2"), text("c2")));
    return 0;
}

int mass(const QStringList &arguments) {
    Command command("mass", "Molarity: give all but one of --mass, --conc and --vol (with --mw to cross between "
                            "grams and moles) and get the rest.");
    command.parser.addOption({"mw", "Molecular weight in g/mol", "g/mol"});
    command.parser.addOption({"mass", "Mass, such as 8.77g", "mass"});
    command.parser.addOption({"moles", "Amount, such as 150mmol", "amount"});
    command.parser.addOption({"conc", "Concentration, molar or mass, such as 150mM or 5mg/mL", "conc"});
    command.parser.addOption({"vol", "Volume, such as 1L", "volume"});
    command.parser.addOption({"in", "Unit to answer in", "unit"});
    command.parser.process(arguments);
    Settings settings;
    if (!command.settings(&settings))
        return 1;
    QMap<QString, Field> fields;
    const QMap<QString, QString> options{{"mass", "mass"}, {"moles", "moles"}, {"concentration", "conc"}, {"volume", "vol"}};
    for (auto it = options.begin(); it != options.end(); ++it)
        if (!command.field(it.value(), &fields[it.key()]))
            return 1;
    std::optional<double> mw;
    if (command.parser.isSet("mw")) {
        mw = Units::parseNumber(command.parser.value("mw"));
        if (!mw)
            return fail("--mw: “" + command.parser.value("mw") + "” isn't a number");
    }
    std::optional<Unit> unit;
    if (!answerUnit(command, &unit))
        return 1;
    // --in answers the one unknown it fits.
    if (unit)
        for (auto it = options.begin(); it != options.end(); ++it) {
            Field &field = fields[it.key()];
            const Dimension d = unit->dimension;
            const bool fits = it.key() == "concentration"
                                  ? d == Dimension::Molar || d == Dimension::MassConcentration
                                  : (it.key() == "mass" && d == Dimension::Mass) ||
                                        (it.key() == "moles" && d == Dimension::Amount) ||
                                        (it.key() == "volume" && d == Dimension::Volume);
            if (!field.isGiven() && fits)
                field.unit = unit;
        }

    const Answer answer = solveMolarity(fields, mw, settings);
    if (!answer.ok)
        return fail(answer.error);
    const int figures = settings.significantFigures;
    if (command.isJson()) {
        QJsonObject object = json(answer, figures);
        if (answer.values.contains("mw")) {
            QJsonObject values = object.value("values").toObject();
            values.insert("mw", QJsonObject{{"value", answer.values["mw"].value}, {"unit", "g/mol"}});
            object.insert("values", values);
        }
        print(object);
        return 0;
    }
    warn(answer);
    for (const QString key : {"mass", "moles", "concentration", "volume"})
        if (answer.values.contains(key))
            print(stdout, QString("%1%2%3").arg(key + ":", -15).arg(Units::format(answer.values[key], figures))
                              .arg(answer.solved.contains(key) ? "  ←" : ""));
    if (answer.solved.contains("mw"))
        print(stdout, QString("%1%2 g/mol  ←").arg("mw:", -15).arg(Units::formatNumber(answer.values["mw"].value, figures)));
    return 0;
}

int serial(const QStringList &arguments) {
    Command command("serial", "Plan a serial dilution: each tube gets --vol from the one before and diluent "
                              "to make 1:factor.");
    command.parser.addOption({"start", "Starting concentration, such as 1mM", "conc"});
    command.parser.addOption({"factor", "Dilution factor per step, such as 10 or 1:10", "n"});
    command.parser.addOption({"steps", "Number of tubes", "n"});
    command.parser.addOption({"vol", "Volume moved from tube to tube, such as 100uL", "volume"});
    command.parser.process(arguments);
    Settings settings;
    if (!command.settings(&settings))
        return 1;
    for (const QString option : {"start", "factor", "steps", "vol"})
        if (!command.parser.isSet(option))
            return fail("Missing --" + option + ".");
    QString error;
    const auto start = Units::parse(command.parser.value("start"), &error);
    if (!start)
        return fail("--start: " + error);
    const auto transfer = Units::parse(command.parser.value("vol"), &error);
    if (!transfer)
        return fail("--vol: " + error);
    QString factorText = command.parser.value("factor").trimmed();
    if (factorText.startsWith("1:"))
        factorText = factorText.mid(2);
    const auto factor = Units::parseNumber(factorText);
    if (!factor)
        return fail("--factor: “" + command.parser.value("factor") + "” isn't a number");
    bool ok = false;
    const int steps = command.parser.value("steps").toInt(&ok);
    if (!ok)
        return fail("--steps must be a whole number.");

    const SerialPlan plan = planSerial(*start, *factor, steps, *transfer, settings);
    if (!plan.answer.ok)
        return fail(plan.answer.error);
    const int figures = settings.significantFigures;
    auto text = [&](const Quantity &q) { return Units::format(q, figures); };
    if (command.isJson()) {
        QJsonArray tubes;
        for (const auto &tube : plan.tubes)
            tubes << QJsonObject{{"tube", tube.index},
                                 {"fold", tube.fold},
                                 {"concentration", json(tube.concentration, figures)},
                                 {"diluent", json(tube.diluent, figures)},
                                 {"transfer", json(tube.transfer, figures)},
                                 {"finalVolume", json(tube.finalVolume, figures)}};
        print({{"tubes", tubes},
               {"stockNeeded", json(plan.stockNeeded, figures)},
               {"totalDiluent", json(plan.totalDiluent, figures)},
               {"warnings", QJsonArray::fromStringList(plan.answer.warnings)}});
        return 0;
    }
    warn(plan.answer);
    print(stdout, "tube\tconcentration\tdilution\tdiluent\ttransfer\tfinal volume");
    for (const auto &tube : plan.tubes)
        print(stdout, QString("%1\t%2\t1:%3\t%4\t%5\t%6")
                          .arg(tube.index)
                          .arg(text(tube.concentration), Units::formatNumber(tube.fold, figures), text(tube.diluent),
                               text(tube.transfer), text(tube.finalVolume)));
    print(stdout, QString("Stock needed: %1, diluent in all: %2").arg(text(plan.stockNeeded), text(plan.totalDiluent)));
    return 0;
}

int recipe(const QStringList &arguments) {
    Command command("recipe", "Work out the amounts of a buffer recipe from a .dilute file or a preset, "
                              "optionally scaled to --vol.");
    command.parser.addPositionalArgument("recipe", "A .dilute file or a preset: " + Presets::names().join(", "),
                                         "<file|preset>");
    command.parser.addOption({"vol", "Final volume to make, such as 500mL", "volume"});
    command.parser.addOption({"save", "Also write the (scaled) recipe as a .dilute file", "file"});
    command.parser.addOption({"list", "List the presets"});
    command.parser.process(arguments);
    if (command.parser.isSet("list")) {
        if (command.isJson())
            print({{"presets", QJsonArray::fromStringList(Presets::names())}});
        else
            for (const auto &name : Presets::names())
                print(stdout, name);
        return 0;
    }
    Settings settings;
    if (!command.settings(&settings))
        return 1;
    const QString source = command.parser.positionalArguments().value(1);
    if (source.isEmpty())
        return fail("Name a .dilute file or a preset (" + Presets::names().join(", ") + ").");
    Recipe recipe;
    QString error;
    if (QFileInfo::exists(source)) {
        if (!RecipeFile::load(source, &recipe, &error))
            return fail(source + ": " + error);
    } else if (const auto preset = Presets::find(source)) {
        recipe = *preset;
    } else {
        return fail("No file or preset called " + source + ". Presets: " + Presets::names().join(", "));
    }
    if (command.parser.isSet("vol")) {
        const auto volume = Units::parse(command.parser.value("vol"), &error);
        if (!volume)
            return fail("--vol: " + error);
        if (volume->unit.dimension != Dimension::Volume)
            return fail("--vol: " + volume->unit.symbol + " isn't a volume");
        recipe = scaled(recipe, *volume);
    }

    const RecipePlan plan = planRecipe(recipe, settings);
    const int figures = settings.significantFigures;
    QStringList problems;
    for (int i = 0; i < plan.amounts.size(); ++i)
        if (!plan.amounts[i].ok)
            problems << recipe.components[i].name + ": " + plan.amounts[i].error;
    if (!plan.answer.ok && problems.isEmpty())
        problems << plan.answer.error;
    if (!problems.isEmpty()) {
        for (const auto &problem : problems)
            print(stderr, problem);
        return 1;
    }
    if (command.parser.isSet("save") && !RecipeFile::save(command.parser.value("save"), recipe, &error))
        return fail("Couldn't write " + command.parser.value("save") + ": " + error);

    if (command.isJson()) {
        QJsonArray components;
        for (int i = 0; i < recipe.components.size(); ++i) {
            const Component &component = recipe.components[i];
            QJsonObject object{{"name", component.name},
                               {"final", Units::serialize(component.final)},
                               {"add", json(plan.amounts[i].amount, figures)},
                               {"kind", plan.amounts[i].isVolume ? "volume" : "mass"}};
            if (component.stock)
                object.insert("stock", Units::serialize(*component.stock));
            if (component.mw > 0)
                object.insert("mw", component.mw);
            components << object;
        }
        print({{"name", recipe.name},
               {"volume", json(recipe.volume, figures)},
               {"components", components},
               {"water", json(plan.water, figures)},
               {"notes", recipe.notes},
               {"warnings", QJsonArray::fromStringList(plan.answer.warnings)}});
        return 0;
    }
    warn(plan.answer);
    print(stdout, recipe.name + ", " + Units::format(recipe.volume, figures));
    for (int i = 0; i < recipe.components.size(); ++i) {
        const Component &component = recipe.components[i];
        const QString concentration = Units::serialize(component.final) +
                                      (component.stock ? " from " + Units::serialize(*component.stock) : QString());
        print(stdout, QString("  %1  %2  %3")
                          .arg(component.name, -22)
                          .arg(concentration, -20)
                          .arg(Units::format(plan.amounts[i].amount, figures)));
    }
    print(stdout, QString("  %1  %2  to %3").arg("Water", -22).arg("", -20).arg(Units::format(recipe.volume, figures)));
    if (!recipe.notes.isEmpty())
        print(stdout, recipe.notes);
    return 0;
}

int convert(const QStringList &arguments) {
    Command command("convert", "Convert a quantity to another unit, with --mw between molar and mass.");
    command.parser.addPositionalArgument("quantity", "Such as 150mM or 5%w/v", "<quantity>");
    command.parser.addOption({"to", "Unit to convert to", "unit"});
    command.parser.addOption({"mw", "Molecular weight in g/mol", "g/mol"});
    command.parser.process(arguments);
    Settings settings;
    if (!command.settings(&settings))
        return 1;
    QString error;
    const auto quantity = Units::parse(command.parser.positionalArguments().value(1), &error);
    if (!quantity)
        return fail(error);
    if (!command.parser.isSet("to"))
        return fail("Missing --to.");
    const auto unit = Units::find(command.parser.value("to"));
    if (!unit)
        return fail("--to: unknown unit “" + command.parser.value("to") + "”");
    double mw = 0;
    if (command.parser.isSet("mw")) {
        const auto parsed = Units::parseNumber(command.parser.value("mw"));
        if (!parsed)
            return fail("--mw: “" + command.parser.value("mw") + "” isn't a number");
        mw = *parsed;
    }
    const auto value = Units::convert(*quantity, *unit, mw, &error);
    if (!value)
        return fail(error);
    const Quantity result{*value, *unit};
    if (command.isJson())
        print(json(result, settings.significantFigures));
    else
        print(stdout, Units::format(result, settings.significantFigures));
    return 0;
}

// As Hype does: one shared copy, linked into Claude Code, found directly by other agents.
int skill(const QStringList &arguments) {
    QCommandLineParser parser;
    parser.setApplicationDescription("Print the agent skill, or install it for your coding agents with: skill install.");
    parser.addHelpOption();
    parser.addPositionalArgument("skill", "", "skill");
    parser.addPositionalArgument("install", "Copy to ~/.agents/skills/dilute and link into ~/.claude/skills",
                                 "[install]");
    parser.process(arguments);
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray text = bundled.readAll();
    const QString action = parser.positionalArguments().value(1);
    if (action.isEmpty()) {
        fputs(text.constData(), stdout);
        return 0;
    }
    if (action != "install")
        return fail("Use dilute skill to print the skill, or dilute skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/dilute");
    QSaveFile file(directory + "/SKILL.md");
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        return fail("Could not write " + file.fileName());
    print(stdout, "Installed " + file.fileName());
    if (!home.exists(".claude"))
        return 0;
    const QString link = home.filePath(".claude/skills/dilute");
    const QFileInfo existing(link);
    if (existing.isSymLink())
        QFile::remove(link);
    else if (existing.exists())
        return fail(link + " exists and is not a link; leaving it alone.");
    if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/dilute", link))
        return fail("Could not link " + link);
    print(stdout, "Linked " + link);
    return 0;
}

int help(const QStringList &arguments) {
    const QString topic = arguments.value(2);
    if (isCliCommand(topic) && topic != "help" && !topic.startsWith('-'))
        return runCli({arguments[0], topic, "--help"});
    print(stdout, "Usage: dilute <command> [options]\n\n" + cliSummary());
    return 0;
}
} // namespace

QString cliSummary() {
    return "Window:\n"
           "  open [recipe.dilute]            The calculators, optionally with a recipe open\n\n"
           "Commands that need no display (dilute help <command> for options):\n"
           "  c1v1 --c1 --v1 --c2 --v2        Give three, get the fourth and the diluent\n"
           "  mass --mw --mass --conc --vol   Molarity: give all but one, get the rest\n"
           "  serial --start --factor --steps --vol   Plan a serial dilution\n"
           "  recipe <file|preset> [--vol]    Amounts for a buffer, scaled to a volume\n"
           "  convert <quantity> --to <unit>  Convert units (--mw between molar and mass)\n"
           "  skill [install]                 Print the skill for coding agents, or install it\n\n"
           "Quantities are a number and a unit: 10mM, 50mL, 100uL, 5%w/v, 10X.\n"
           "Every command takes --json, exits 0 on success and 1 on failure, with errors on stderr.";
}

bool isCliCommand(const QString &word) {
    return QStringList{"c1v1", "mass", "molarity", "serial", "recipe", "convert", "skill", "help", "--help", "-h"}
        .contains(word);
}

int runCli(const QStringList &arguments) {
    // Commands print C-locale numbers, whatever the desktop's language, so
    // scripts can read them.
    QLocale::setDefault(QLocale::c());
    const QString command = arguments.value(1);
    return command == "c1v1"                            ? c1v1(arguments)
           : command == "mass" || command == "molarity" ? mass(arguments)
           : command == "serial"                        ? serial(arguments)
           : command == "recipe"                        ? recipe(arguments)
           : command == "convert"                       ? convert(arguments)
           : command == "skill"                         ? skill(arguments)
                                                        : help(arguments);
}
