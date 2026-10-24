#include "backend.h"

#include <QClipboard>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

#include "card.h"
#include "filepicker.h"
#include "recipefile.h"

namespace {
constexpr int historyLimit = 50;

QVariantMap shown(const Quantity &quantity, int figures) {
    return {{"number", Units::formatNumber(quantity.value, figures)},
            {"unit", quantity.unit.symbol},
            {"text", Units::format(quantity, figures)},
            // Exact and in C, for building a .dilute recipe from the result.
            {"value", quantity.value},
            {"serialized", Units::serialize(quantity)}};
}

QVariantMap common(const Answer &answer) {
    return {{"ok", answer.ok},
            {"incomplete", answer.incomplete},
            {"error", answer.error},
            {"warnings", answer.warnings},
            {"solved", answer.solved}};
}

QVariantMap failed(const QString &error) { return {{"ok", false}, {"incomplete", false}, {"error", error}}; }

// A form field arrives as {text, unit}. Empty text is the one to work out;
// text that isn't a number in the Omarchy language (or plain C) is an error,
// never a zero.
bool readField(const QVariant &value, const QString &label, Field *field, QString *error) {
    const QVariantMap map = value.toMap();
    const QString text = map.value("text").toString().trimmed();
    const QString symbol = map.value("unit").toString();
    *field = Field::asked(symbol);
    if (!symbol.isEmpty() && !Units::find(symbol)) {
        *error = QObject::tr("%1: unknown unit “%2”").arg(label, symbol);
        return false;
    }
    if (text.isEmpty())
        return true;
    const auto number = Units::parseNumber(text, QLocale());
    if (!number) {
        *error = QObject::tr("%1: “%2” isn't a number").arg(label, text);
        return false;
    }
    field->value = *number;
    return true;
}

QJsonDocument readJson(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll());
}

void writeJson(const QString &path, const QJsonDocument &document) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QString error;
    writeAtomically(path, document.toJson(), &error);
}
} // namespace

Backend::Paths Backend::Paths::standard() {
    return {QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/dilute",
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/dilute",
            QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/dilute"};
}

Backend::Backend(FilePicker *picker, const Paths &paths, QObject *parent)
    : QObject(parent), m_picker(picker), m_paths(paths) {
    const QJsonObject settings = readJson(m_paths.config + "/settings.json").object();
    m_settings.significantFigures = std::clamp(settings.value("significantFigures").toInt(3), 1, 8);
    const double minimum = settings.value("minimumPipetteMicroliters").toDouble(0.5);
    m_settings.minimumPipetteLiters = std::max(0.0, minimum) * 1e-6;

    m_history = readJson(m_paths.state + "/history.json").array().toVariantList();

    const QJsonDocument reagents = readJson(m_paths.data + "/reagents.json");
    if (reagents.isArray()) {
        for (const auto &item : reagents.array())
            m_reagents << Reagent{item.toObject().value("name").toString(), item.toObject().value("mw").toDouble()};
    } else {
        m_reagents = builtInReagents();
    }

    if (m_picker)
        connect(m_picker, &FilePicker::failed, this, [this](const QString &error) { notify(error, true); });
}

void Backend::setSignificantFigures(int figures) {
    figures = std::clamp(figures, 1, 8);
    if (figures == m_settings.significantFigures)
        return;
    m_settings.significantFigures = figures;
    saveSettings();
    emit settingsChanged();
}

void Backend::setMinimumPipette(double microliters) {
    const double liters = std::max(0.0, microliters) * 1e-6;
    if (std::isnan(microliters) || liters == m_settings.minimumPipetteLiters)
        return;
    m_settings.minimumPipetteLiters = liters;
    saveSettings();
    emit settingsChanged();
}

void Backend::saveSettings() {
    writeJson(m_paths.config + "/settings.json",
              QJsonDocument(QJsonObject{{"significantFigures", m_settings.significantFigures},
                                        {"minimumPipetteMicroliters", m_settings.minimumPipetteLiters * 1e6}}));
}

QStringList Backend::units(const QString &kind) const {
    if (kind == "concentration")
        return Units::concentrationSymbols();
    if (kind == "solution")
        return Units::symbols(Dimension::Molar) + Units::symbols(Dimension::MassConcentration);
    if (kind == "volume")
        return Units::symbols(Dimension::Volume);
    if (kind == "mass")
        return Units::symbols(Dimension::Mass);
    if (kind == "amount")
        return Units::symbols(Dimension::Amount);
    return {};
}

QString Backend::number(double value) const { return Units::formatNumber(value, m_settings.significantFigures); }

QString Backend::today() { return QLocale().toString(QDate::currentDate(), QLocale::LongFormat); }

QVariantMap Backend::dilution(const QVariantMap &inputs) const {
    QMap<QString, Field> fields;
    for (const QString key : {"c1", "v1", "c2", "v2"}) {
        QString error;
        if (!readField(inputs.value(key), key.toUpper(), &fields[key], &error))
            return failed(error);
    }
    const Answer answer = solveDilution(fields, m_settings);
    QVariantMap result = common(answer);
    if (!answer.ok)
        return result;

    const int figures = m_settings.significantFigures;
    QVariantMap values;
    for (auto it = answer.values.begin(); it != answer.values.end(); ++it)
        values[it.key()] = shown(it.value(), figures);
    result["values"] = values;

    auto text = [&](const QString &key) { return Units::format(answer.values[key], figures); };
    const QString sentence = tr("Add %1 of stock at %2 to %3 of diluent: %4 at %5.")
                                 .arg(text("v1"), text("c1"), text("diluent"), text("v2"), text("c2"));
    result["sentence"] = sentence;
    result["main"] = answer.solved.isEmpty() ? text("diluent") : text(answer.solved.first());
    result["summary"] = sentence;
    result["card"] = Card{tr("Dilution"),
                          today() + "  ·  C1V1 = C2V2",
                          {tr("Step"), tr("Add")},
                          {{tr("Stock at %1").arg(text("c1")), text("v1")}, {tr("Diluent"), text("diluent")}},
                          QStringList{tr("Makes %1 at %2.").arg(text("v2"), text("c2"))} + answer.warnings}
                         .toVariant();
    return result;
}

QVariantMap Backend::molarity(const QVariantMap &inputs) const {
    QMap<QString, Field> fields;
    const QMap<QString, QString> labels{
        {"mass", tr("Mass")}, {"moles", tr("Moles")}, {"concentration", tr("Concentration")}, {"volume", tr("Volume")}};
    for (auto it = labels.begin(); it != labels.end(); ++it) {
        QString error;
        if (!readField(inputs.value(it.key()), it.value(), &fields[it.key()], &error))
            return failed(error);
    }
    std::optional<double> mw;
    const QString mwText = inputs.value("mw").toString().trimmed();
    if (!mwText.isEmpty()) {
        mw = Units::parseNumber(mwText, QLocale());
        if (!mw)
            return failed(tr("%1: “%2” isn't a number").arg(tr("Molecular weight"), mwText));
    }

    const Answer answer = solveMolarity(fields, mw, m_settings);
    QVariantMap result = common(answer);
    if (!answer.ok)
        return result;

    const int figures = m_settings.significantFigures;
    QVariantMap values;
    for (auto it = answer.values.begin(); it != answer.values.end(); ++it)
        values[it.key()] = it.key() == "mw" ? QVariantMap{{"number", Units::formatNumber(it.value().value, figures)},
                                                          {"unit", "g/mol"},
                                                          {"value", it.value().value},
                                                          {"text", Units::formatNumber(it.value().value, figures) +
                                                                       " g/mol"}}
                                            : shown(it.value(), figures);
    result["values"] = values;

    auto text = [&](const QString &key) { return Units::format(answer.values[key], figures); };
    const QString solute = inputs.value("reagent").toString().trimmed();
    const QString name = solute.isEmpty() ? tr("solute") : solute;
    const QString sentence =
        tr("Dissolve %1 of %2 in water to %3 for %4.").arg(text("mass"), name, text("volume"), text("concentration"));
    result["sentence"] = sentence;
    QString main = text("mass");
    for (const auto &key : answer.solved)
        if (key != "moles" && key != "mw") {
            main = text(key);
            break;
        }
    result["main"] = main;
    result["summary"] = sentence;
    QStringList notes{tr("Final concentration: %1").arg(text("concentration"))};
    if (answer.values.contains("moles"))
        notes << tr("Amount: %1").arg(text("moles"));
    result["card"] = Card{solute.isEmpty() ? tr("Solution") : solute,
                          today(),
                          {tr("Step"), tr("Add")},
                          {{tr("Weigh %1").arg(name), text("mass")}, {tr("Water to"), text("volume")}},
                          notes + answer.warnings}
                         .toVariant();
    return result;
}

QVariantMap Backend::serial(const QVariantMap &inputs) const {
    Field start, transfer;
    QString error;
    if (!readField(inputs.value("start"), tr("Start"), &start, &error) ||
        !readField(inputs.value("transfer"), tr("Transfer"), &transfer, &error))
        return failed(error);
    QString factorText = inputs.value("factor").toString().trimmed();
    // "1:10" means a factor of 10.
    if (factorText.startsWith("1:"))
        factorText = factorText.mid(2).trimmed();
    const int steps = inputs.value("steps").toInt();
    if (!start.isGiven() || !transfer.isGiven() || factorText.isEmpty())
        return {{"ok", false},
                {"incomplete", true},
                {"error", tr("Fill in the start concentration, factor and transfer volume")}};
    const auto factor = Units::parseNumber(factorText, QLocale());
    if (!factor)
        return failed(tr("%1: “%2” isn't a number").arg(tr("Factor"), factorText));

    const SerialPlan plan = planSerial(*start.quantity(), *factor, steps, *transfer.quantity(), m_settings);
    QVariantMap result = common(plan.answer);
    if (!plan.answer.ok)
        return result;

    const int figures = m_settings.significantFigures;
    auto text = [&](const Quantity &quantity) { return Units::format(quantity, figures); };
    QVariantList rows;
    QList<QStringList> cardRows;
    QStringList tsv{QStringList{tr("Tube"), tr("Concentration"), tr("Dilution"), tr("Diluent"), tr("Transfer"),
                                tr("Final volume")}.join('\t')};
    for (const auto &tube : plan.tubes) {
        const QString fold = "1:" + Units::formatCompact(tube.fold, figures);
        rows << QVariantMap{{"tube", QLocale().toString(tube.index)},
                            {"concentration", text(tube.concentration)},
                            {"fold", fold},
                            {"diluent", text(tube.diluent)},
                            {"transfer", text(tube.transfer)},
                            {"finalVolume", text(tube.finalVolume)}};
        cardRows << QStringList{tr("Tube %1").arg(QLocale().toString(tube.index)), fold, text(tube.diluent),
                                text(tube.concentration)};
        tsv << QStringList{QString::number(tube.index), text(tube.concentration), fold, text(tube.diluent),
                           text(tube.transfer), text(tube.finalVolume)}.join('\t');
    }
    result["rows"] = rows;
    const QString sentence = tr("Fill %n tubes with %1 of diluent, then move %2 down the series, mixing each time.",
                                nullptr, plan.tubes.size())
                                 .arg(text(plan.tubes.first().diluent), text(plan.tubes.first().transfer));
    result["sentence"] = sentence;
    result["details"] = tr("Needs %1 of stock and %2 of diluent in all.")
                            .arg(text(plan.stockNeeded), text(plan.totalDiluent));
    result["main"] = tsv.join('\n');
    result["summary"] = tr("Serial 1:%1 from %2, %n tubes", nullptr, plan.tubes.size())
                            .arg(Units::formatCompact(*factor, figures), text(*start.quantity()));
    result["card"] = Card{tr("Serial dilution 1:%1").arg(Units::formatCompact(*factor, figures)),
                          today() + "  ·  " + tr("from %1").arg(text(*start.quantity())),
                          {tr("Tube"), tr("Dilution"), tr("Diluent"), tr("Concentration")},
                          cardRows,
                          QStringList{sentence,
                                      tr("The last tube keeps the final transfer; discard %1 to match the others.")
                                          .arg(text(plan.tubes.first().transfer))} +
                              plan.answer.warnings}
                         .toVariant();
    return result;
}

void Backend::copy(const QString &text) {
    if (text.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(text);
    const QString first = text.section('\n', 0, 0);
    notify(text.contains('\n') ? tr("Copied the table") : tr("Copied %1").arg(first));
}

void Backend::remember(int tab, const QVariantMap &inputs, const QString &summary) {
    if (summary.isEmpty())
        return;
    const QDateTime now = QDateTime::currentDateTime();
    const QVariantMap entry{{"time", now.toString(Qt::ISODate)}, {"tab", tab}, {"summary", summary}, {"inputs", inputs}};
    // Live results would add an entry per keystroke, so a recent entry from
    // the same calculator is the same calculation still being typed.
    if (!m_history.isEmpty()) {
        const QVariantMap last = m_history.first().toMap();
        const QDateTime then = QDateTime::fromString(last.value("time").toString(), Qt::ISODate);
        if (last.value("summary") == summary)
            return;
        if (last.value("tab").toInt() == tab && then.secsTo(now) < 30)
            m_history.removeFirst();
    }
    m_history.prepend(entry);
    while (m_history.size() > historyLimit)
        m_history.removeLast();
    saveHistory();
    emit historyChanged();
}

void Backend::clearHistory() {
    m_history.clear();
    saveHistory();
    emit historyChanged();
}

void Backend::saveHistory() {
    writeJson(m_paths.state + "/history.json", QJsonDocument(QJsonArray::fromVariantList(m_history)));
}

QVariantList Backend::reagents() const {
    QVariantList list;
    for (const auto &reagent : m_reagents)
        list << QVariantMap{{"name", reagent.name},
                            {"mw", reagent.mw},
                            {"mwText", reagent.mw > 0 ? Units::formatNumber(reagent.mw, 6) : QString()}};
    return list;
}

void Backend::setReagent(int index, const QString &name, const QString &mw) {
    if (index < 0 || index >= m_reagents.size())
        return;
    double weight = 0;
    if (!mw.trimmed().isEmpty()) {
        const auto parsed = Units::parseNumber(mw, QLocale());
        if (!parsed || *parsed <= 0) {
            notify(tr("%1: “%2” isn't a number").arg(tr("Molecular weight"), mw), true);
            return;
        }
        weight = *parsed;
    }
    if (m_reagents[index].name == name && m_reagents[index].mw == weight)
        return;
    m_reagents[index] = {name, weight};
    saveReagents();
    emit reagentsChanged();
}

void Backend::addReagent() {
    m_reagents.append({tr("New reagent"), 0});
    saveReagents();
    emit reagentsChanged();
}

void Backend::removeReagent(int index) {
    if (index < 0 || index >= m_reagents.size())
        return;
    m_reagents.removeAt(index);
    saveReagents();
    emit reagentsChanged();
}

void Backend::resetReagents() {
    m_reagents = builtInReagents();
    QFile::remove(m_paths.data + "/reagents.json");
    emit reagentsChanged();
}

void Backend::saveReagents() {
    QJsonArray list;
    for (const auto &reagent : m_reagents)
        list << QJsonObject{{"name", reagent.name}, {"mw", reagent.mw}};
    writeJson(m_paths.data + "/reagents.json", QJsonDocument(list));
}

bool Backend::writeCard(const QString &path, const QVariantMap &card, const QString &format, QString *error) {
    const Card painted = Card::fromVariant(card);
    return format == "svg" ? writeCardSvg(path, painted, error) : writeCardPdf(path, painted, error);
}

void Backend::exportCard(const QVariantMap &card, const QString &format) {
    if (!m_picker || card.isEmpty())
        return;
    const QString suffix = format == "svg" ? "svg" : "pdf";
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString name = card.value("title").toString().trimmed();
    name.replace(QRegularExpression("[/\\\\:*?\"<>|]"), "-");
    if (name.isEmpty())
        name = "recipe";
    const QString suggested = (QDir(documents).exists() ? documents : QDir::homePath()) + "/" + name + "." + suffix;
    const FilePicker::Filter filter = suffix == "svg" ? FilePicker::Filter{tr("SVG image"), {"*.svg"}}
                                                      : FilePicker::Filter{tr("PDF document"), {"*.pdf"}};
    m_picker->save(tr("Export recipe card"), suggested, {filter}, [this, card, suffix](QString path) {
        if (QFileInfo(path).suffix().compare(suffix, Qt::CaseInsensitive) != 0)
            path += "." + suffix;
        QString error;
        if (writeCard(path, card, suffix, &error))
            notify(tr("Exported %1").arg(QFileInfo(path).fileName()));
        else
            notify(tr("Couldn't write %1: %2").arg(path, error), true);
    });
}

void Backend::saveRecipe(const QVariantMap &map) {
    Recipe recipe;
    QString error;
    if (!RecipeFile::fromJson(QJsonObject::fromVariantMap(map), &recipe, &error)) {
        notify(error, true);
        return;
    }
    if (!m_picker)
        return;
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString name = recipe.name.trimmed();
    name.replace(QRegularExpression("[/\\\\:*?\"<>|]"), "-");
    if (name.isEmpty())
        name = "recipe";
    const QString suggested = (QDir(documents).exists() ? documents : QDir::homePath()) + "/" + name + ".dilute";
    m_picker->save(tr("Save recipe"), suggested, {{tr("dilute recipe"), {"*.dilute"}}}, [this, recipe](QString path) {
        if (QFileInfo(path).suffix() != "dilute")
            path += ".dilute";
        QString error;
        if (RecipeFile::save(path, recipe, &error))
            notify(tr("Saved %1").arg(QFileInfo(path).fileName()));
        else
            notify(tr("Couldn't write %1: %2").arg(path, error), true);
    });
}
