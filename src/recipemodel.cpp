#include "recipemodel.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include "backend.h"
#include "card.h"
#include "filepicker.h"
#include "recipefile.h"

namespace {
// Edits to the same field within this long undo together, so undo doesn't
// step back one keystroke at a time.
constexpr int coalesceMilliseconds = 1500;

// Numbers as people typed them in a recipe: no rounding to significant
// figures, which would turn 2.7 mM into a falsely precise 2.70 mM.
QString plain(double value) {
    QLocale locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    return locale.toString(value, 'g', 12);
}

QString plain(const Quantity &quantity) { return plain(quantity.value) + " " + quantity.unit.symbol; }

bool blank(const RecipeModel::Row &row) {
    return row.name.trimmed().isEmpty() && row.mw.trimmed().isEmpty() && row.finalText.trimmed().isEmpty() &&
           row.stockText.trimmed().isEmpty();
}

// The draft as a recipe of its filled-in rows; indices maps each component
// back to its row.
Recipe toRecipe(const RecipeModel::Draft &draft, QStringList *errors, QList<int> *indices) {
    Recipe recipe;
    recipe.name = draft.name.trimmed();
    recipe.notes = draft.notes;
    const auto volume = Units::parseNumber(draft.volumeText, QLocale());
    recipe.volume = {volume.value_or(0), Units::unit(draft.volumeUnit)};
    for (int i = 0; i < draft.rows.size(); ++i) {
        const auto &row = draft.rows[i];
        if (blank(row))
            continue;
        Component component;
        QString error;
        component.name = row.name.trimmed();
        if (!row.mw.trimmed().isEmpty()) {
            const auto mw = Units::parseNumber(row.mw, QLocale());
            if (mw && *mw > 0)
                component.mw = *mw;
            else
                error = QObject::tr("MW: “%1” isn't a number").arg(row.mw.trimmed());
        }
        const auto final = Units::parseNumber(row.finalText, QLocale());
        if (row.finalText.trimmed().isEmpty())
            error = QObject::tr("Enter the final concentration");
        else if (!final)
            error = QObject::tr("“%1” isn't a number").arg(row.finalText.trimmed());
        component.final = {final.value_or(0), Units::unit(row.finalUnit)};
        if (!row.stockText.trimmed().isEmpty()) {
            const auto stock = Units::parseNumber(row.stockText, QLocale());
            if (!stock)
                error = QObject::tr("Stock: “%1” isn't a number").arg(row.stockText.trimmed());
            component.stock = Quantity{stock.value_or(0), Units::unit(row.stockUnit)};
        }
        recipe.components << component;
        if (errors)
            *errors << error;
        if (indices)
            *indices << i;
    }
    return recipe;
}

RecipeModel::Draft toDraft(const Recipe &recipe) {
    RecipeModel::Draft draft;
    draft.name = recipe.name;
    draft.notes = recipe.notes;
    draft.volumeText = plain(recipe.volume.value);
    draft.volumeUnit = recipe.volume.unit.symbol;
    for (const auto &component : recipe.components) {
        RecipeModel::Row row;
        row.name = component.name;
        row.mw = component.mw > 0 ? plain(component.mw) : QString();
        row.finalText = plain(component.final.value);
        row.finalUnit = component.final.unit.symbol;
        if (component.stock) {
            row.stockText = plain(component.stock->value);
            row.stockUnit = component.stock->unit.symbol;
        }
        draft.rows << row;
    }
    return draft;
}

QString fileSafe(QString name) {
    name.replace(QRegularExpression("[/\\\\:*?\"<>|]"), "-");
    return name.trimmed();
}
} // namespace

RecipeModel::RecipeModel(Backend *backend, QObject *parent) : QAbstractListModel(parent), m_backend(backend) {
    m_draft.rows << Row{};
    m_saved = m_draft;
    m_sinceEdit.start();
    connect(m_backend, &Backend::settingsChanged, this, &RecipeModel::recompute);
    recompute();
}

int RecipeModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_draft.rows.size(); }

QVariant RecipeModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_draft.rows.size())
        return {};
    const Row &row = m_draft.rows[index.row()];
    const ComponentAmount amount = m_amounts.value(index.row());
    switch (role) {
    case NameRole: return row.name;
    case MwRole: return row.mw;
    case FinalTextRole: return row.finalText;
    case FinalUnitRole: return row.finalUnit;
    case StockTextRole: return row.stockText;
    case StockUnitRole: return row.stockUnit;
    case AmountRole:
        return amount.ok ? Units::format(amount.amount, m_backend->settings().significantFigures) : QString();
    case IsVolumeRole: return amount.isVolume;
    case ErrorRole: return amount.error;
    case WarningRole: return amount.warning;
    }
    return {};
}

QHash<int, QByteArray> RecipeModel::roleNames() const {
    return {{NameRole, "name"},          {MwRole, "mw"},           {FinalTextRole, "finalText"},
            {FinalUnitRole, "finalUnit"}, {StockTextRole, "stockText"}, {StockUnitRole, "stockUnit"},
            {AmountRole, "amount"},      {IsVolumeRole, "isVolume"}, {ErrorRole, "error"},
            {WarningRole, "warning"}};
}

QString RecipeModel::fileName() const { return m_path.isEmpty() ? QString() : QFileInfo(m_path).fileName(); }

void RecipeModel::edit(const QString &key, const std::function<void()> &change) {
    if (key.isEmpty() || key != m_lastEdit || m_sinceEdit.elapsed() > coalesceMilliseconds) {
        m_undo << m_draft;
        m_redo.clear();
    }
    m_lastEdit = key;
    m_sinceEdit.restart();
    change();
    recompute();
}

void RecipeModel::setName(const QString &name) {
    if (name == m_draft.name)
        return;
    edit("name", [&] { m_draft.name = name; });
}

void RecipeModel::setNotes(const QString &notes) {
    if (notes == m_draft.notes)
        return;
    edit("notes", [&] { m_draft.notes = notes; });
}

void RecipeModel::setVolume(const QString &text, const QString &unit) {
    if (text == m_draft.volumeText && unit == m_draft.volumeUnit)
        return;
    edit("volume", [&] {
        m_draft.volumeText = text;
        m_draft.volumeUnit = unit;
    });
}

void RecipeModel::setField(int index, const QString &field, const QString &value) {
    if (index < 0 || index >= m_draft.rows.size())
        return;
    static const QHash<QString, QString Row::*> fields{
        {"name", &Row::name},           {"mw", &Row::mw},           {"finalText", &Row::finalText},
        {"finalUnit", &Row::finalUnit}, {"stockText", &Row::stockText}, {"stockUnit", &Row::stockUnit}};
    const auto member = fields.value(field);
    if (!member || m_draft.rows[index].*member == value)
        return;
    edit(QString("%1/%2").arg(index).arg(field), [&] { m_draft.rows[index].*member = value; });
    // The view may not be what changed it (undo of a coalesced edit, a
    // script), so the cell hears about it too.
    static const QHash<QString, int> roles{{"name", NameRole},           {"mw", MwRole},
                                           {"finalText", FinalTextRole}, {"finalUnit", FinalUnitRole},
                                           {"stockText", StockTextRole}, {"stockUnit", StockUnitRole}};
    emit dataChanged(this->index(index), this->index(index), {roles.value(field)});
}

void RecipeModel::addComponent() {
    edit({}, [&] {
        beginInsertRows({}, m_draft.rows.size(), m_draft.rows.size());
        m_draft.rows << Row{};
        endInsertRows();
    });
}

void RecipeModel::removeComponent(int index) {
    if (index < 0 || index >= m_draft.rows.size())
        return;
    edit({}, [&] {
        beginRemoveRows({}, index, index);
        m_draft.rows.removeAt(index);
        endRemoveRows();
    });
}

void RecipeModel::replace(const Draft &draft) {
    beginResetModel();
    m_draft = draft;
    m_amounts.clear();
    endResetModel();
    recompute();
}

QString RecipeModel::presetDirectory() const { return m_backend->paths().data + "/recipes"; }

QStringList RecipeModel::presets() const {
    QStringList list = Presets::names();
    const QDir directory(presetDirectory());
    for (const auto &file : directory.entryInfoList({"*.dilute"}, QDir::Files, QDir::Name)) {
        Recipe recipe;
        const QString name = RecipeFile::load(file.filePath(), &recipe, nullptr) && !recipe.name.isEmpty()
                                 ? recipe.name
                                 : file.completeBaseName();
        if (!list.contains(name))
            list << name;
    }
    return list;
}

void RecipeModel::loadPreset(const QString &name) {
    // A preset someone saved under a built-in name replaces the built-in.
    const QDir directory(presetDirectory());
    for (const auto &file : directory.entryInfoList({"*.dilute"}, QDir::Files)) {
        Recipe recipe;
        if (RecipeFile::load(file.filePath(), &recipe, nullptr) &&
            (recipe.name == name || file.completeBaseName() == name)) {
            m_undo << m_draft;
            m_redo.clear();
            setRecipe(recipe);
            return;
        }
    }
    if (const auto recipe = Presets::find(name)) {
        m_undo << m_draft;
        m_redo.clear();
        setRecipe(*recipe);
    }
}

void RecipeModel::newRecipe() {
    m_undo << m_draft;
    m_redo.clear();
    Draft draft;
    draft.rows << Row{};
    m_path.clear();
    m_saved = draft;
    m_lastEdit.clear();
    replace(draft);
}

void RecipeModel::undo() {
    if (m_undo.isEmpty())
        return;
    m_redo << m_draft;
    m_lastEdit.clear();
    replace(m_undo.takeLast());
}

void RecipeModel::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo << m_draft;
    m_lastEdit.clear();
    replace(m_redo.takeLast());
}

void RecipeModel::setRecipe(const Recipe &recipe, const QString &path) {
    Draft draft = toDraft(recipe);
    if (draft.rows.isEmpty())
        draft.rows << Row{};
    m_path = path;
    m_saved = draft;
    m_lastEdit.clear();
    replace(draft);
}

Recipe RecipeModel::recipe(QStringList *rowErrors) const { return toRecipe(m_draft, rowErrors, nullptr); }

QVariantMap RecipeModel::snapshot() const { return RecipeFile::toJson(recipe()).toVariantMap(); }

void RecipeModel::restore(const QVariantMap &map) {
    Recipe recipe;
    QString error;
    if (!RecipeFile::fromJson(QJsonObject::fromVariantMap(map), &recipe, &error)) {
        m_backend->notify(error, true);
        return;
    }
    m_undo << m_draft;
    m_redo.clear();
    Draft draft = toDraft(recipe);
    m_path.clear();
    m_lastEdit.clear();
    replace(draft);
}

void RecipeModel::recompute() {
    const Settings &settings = m_backend->settings();
    const int figures = settings.significantFigures;
    QStringList errors;
    QList<int> indices;
    const Recipe recipe = toRecipe(m_draft, &errors, &indices);
    RecipePlan plan = planRecipe(recipe, settings);

    m_amounts = QList<ComponentAmount>(m_draft.rows.size());
    bool rowsOk = true;
    for (int i = 0; i < indices.size(); ++i) {
        ComponentAmount amount = plan.amounts.value(i);
        if (!errors[i].isEmpty()) {
            amount = {};
            amount.error = errors[i];
        }
        rowsOk = rowsOk && amount.ok;
        m_amounts[indices[i]] = amount;
    }
    if (!m_draft.rows.isEmpty())
        emit dataChanged(index(0), index(m_draft.rows.size() - 1),
                         {AmountRole, IsVolumeRole, ErrorRole, WarningRole});

    QVariantMap result{{"ok", plan.answer.ok && rowsOk},
                       {"incomplete", plan.answer.incomplete},
                       {"error", plan.answer.error},
                       {"warnings", plan.answer.warnings}};
    if (plan.answer.error.isEmpty() && !rowsOk)
        result["error"] = tr("Fix the components marked in red");

    const QString volume = Units::format(recipe.volume, figures);
    const QString title = recipe.name.isEmpty() ? tr("Untitled recipe") : recipe.name;
    if (!plan.answer.incomplete && recipe.volume.value > 0) {
        QList<QStringList> rows;
        QStringList copy{title + " — " + volume};
        for (int i = 0; i < recipe.components.size(); ++i) {
            const Component &component = recipe.components[i];
            const ComponentAmount &amount = m_amounts[indices[i]];
            if (!amount.ok)
                continue;
            const QString concentration = component.stock ? tr("%1 from %2").arg(plain(component.final),
                                                                                  plain(*component.stock))
                                                          : plain(component.final);
            const QString added = Units::format(amount.amount, figures);
            rows << QStringList{component.name, concentration, added};
            copy << component.name + "\t" + added;
        }
        rows << QStringList{tr("Water"), QString(), tr("to %1").arg(volume)};
        copy << tr("Water") + "\t" + tr("to %1").arg(volume);
        QStringList notes;
        if (!recipe.notes.trimmed().isEmpty())
            notes << recipe.notes.trimmed();
        notes << plan.answer.warnings;
        result["card"] = Card{title, Backend::today() + "  ·  " + volume, {tr("Component"), tr("Final"), tr("Add")},
                              rows, notes}
                             .toVariant();
        result["main"] = copy.join('\n');
        result["water"] = Units::format(plan.water, figures);
        result["volume"] = volume;
        result["sentence"] = tr("Add the solids and liquids, then bring to %1 with water.").arg(volume);
        result["summary"] =
            tr("%1, %2: %n components", nullptr, recipe.components.size()).arg(title, volume);
    }
    m_result = result;
    emit changed();
}

void RecipeModel::save() {
    if (m_path.isEmpty()) {
        saveAs();
        return;
    }
    if (saveTo(m_path))
        m_backend->notify(tr("Saved %1").arg(fileName()));
}

bool RecipeModel::saveTo(const QString &path) {
    QStringList errors;
    toRecipe(m_draft, &errors, nullptr);
    for (const auto &error : errors)
        if (!error.isEmpty()) {
            m_backend->notify(tr("Fix the components marked in red before saving"), true);
            return false;
        }
    QString error;
    if (!RecipeFile::save(path, recipe(), &error)) {
        m_backend->notify(tr("Couldn't write %1: %2").arg(path, error), true);
        return false;
    }
    m_path = path;
    m_saved = m_draft;
    emit changed();
    return true;
}

void RecipeModel::saveAs() {
    if (!m_backend->picker())
        return;
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString folder = !m_path.isEmpty()         ? QFileInfo(m_path).absolutePath()
                           : QDir(documents).exists() ? documents
                                                      : QDir::homePath();
    const QString name = fileSafe(m_draft.name).isEmpty() ? "recipe" : fileSafe(m_draft.name);
    m_backend->picker()->save(tr("Save recipe"), folder + "/" + name + ".dilute",
                              {{tr("dilute recipe"), {"*.dilute"}}}, [this](QString path) {
                                  if (QFileInfo(path).suffix() != "dilute")
                                      path += ".dilute";
                                  if (saveTo(path))
                                      m_backend->notify(tr("Saved %1").arg(fileName()));
                              });
}

void RecipeModel::open() {
    if (!m_backend->picker())
        return;
    m_backend->picker()->open(tr("Open recipe"), {{tr("dilute recipe"), {"*.dilute"}}},
                              [this](const QString &path) { openPath(path); });
}

bool RecipeModel::openPath(const QString &path) {
    Recipe recipe;
    QString error;
    if (!RecipeFile::load(path, &recipe, &error)) {
        m_backend->notify(tr("Couldn't open %1: %2").arg(QFileInfo(path).fileName(), error), true);
        return false;
    }
    m_undo << m_draft;
    m_redo.clear();
    setRecipe(recipe, QFileInfo(path).absoluteFilePath());
    return true;
}

void RecipeModel::savePreset() {
    const QString name = fileSafe(m_draft.name);
    if (name.isEmpty()) {
        m_backend->notify(tr("Name the recipe to save it as a preset"), true);
        return;
    }
    QStringList errors;
    toRecipe(m_draft, &errors, nullptr);
    for (const auto &error : errors)
        if (!error.isEmpty()) {
            m_backend->notify(tr("Fix the components marked in red before saving"), true);
            return;
        }
    QDir().mkpath(presetDirectory());
    QString error;
    if (!RecipeFile::save(presetDirectory() + "/" + name + ".dilute", recipe(), &error)) {
        m_backend->notify(tr("Couldn't save the preset: %1").arg(error), true);
        return;
    }
    m_saved = m_draft;
    emit changed();
    emit presetsChanged();
    m_backend->notify(tr("Saved “%1” as a preset").arg(m_draft.name.trimmed()));
}
