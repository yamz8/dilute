#include "recipefile.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("dilute", text); }
} // namespace

namespace RecipeFile {

QJsonObject toJson(const Recipe &recipe) {
    QJsonArray components;
    for (const auto &component : recipe.components) {
        QJsonObject object{{"name", component.name}, {"final", Units::serialize(component.final)}};
        if (component.mw > 0)
            object.insert("mw", component.mw);
        if (component.stock)
            object.insert("stock", Units::serialize(*component.stock));
        components << object;
    }
    return {{"format", "dilute"},
            {"version", 1},
            {"name", recipe.name},
            {"notes", recipe.notes},
            {"volume", Units::serialize(recipe.volume)},
            {"components", components}};
}

bool fromJson(const QJsonObject &object, Recipe *recipe, QString *error) {
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (object.value("format").toString() != "dilute")
        return fail(tr("This isn't a dilute recipe"));
    if (object.value("version").toInt(1) > 1)
        return fail(tr("This recipe was made by a newer dilute"));

    Recipe result;
    result.name = object.value("name").toString();
    result.notes = object.value("notes").toString();
    QString problem;
    const auto volume = Units::parse(object.value("volume").toString("1 L"), &problem);
    if (!volume || volume->unit.dimension != Dimension::Volume)
        return fail(tr("The volume: %1").arg(volume ? tr("%1 isn't a volume").arg(volume->unit.symbol) : problem));
    result.volume = *volume;

    const QJsonArray components = object.value("components").toArray();
    for (int i = 0; i < components.size(); ++i) {
        const QJsonObject item = components[i].toObject();
        Component component;
        component.name = item.value("name").toString();
        component.mw = item.value("mw").toDouble(0);
        const QString where = component.name.isEmpty() ? tr("Component %1").arg(i + 1) : component.name;
        const auto final = Units::parse(item.value("final").toString(), &problem);
        if (!final)
            return fail(where + ": " + problem);
        component.final = *final;
        if (item.contains("stock") && !item.value("stock").isNull()) {
            const auto stock = Units::parse(item.value("stock").toString(), &problem);
            if (!stock)
                return fail(where + ": " + problem);
            component.stock = *stock;
        }
        result.components << component;
    }
    *recipe = result;
    return true;
}

bool load(const QString &path, Recipe *recipe, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = tr("Not valid JSON: %1").arg(parse.errorString());
        return false;
    }
    return fromJson(document.object(), recipe, error);
}

bool save(const QString &path, const Recipe &recipe, QString *error) {
    return writeAtomically(path, QJsonDocument(toJson(recipe)).toJson(), error);
}

} // namespace RecipeFile

bool writeAtomically(const QString &path, const QByteArray &data, QString *error) {
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit())
        return true;
    if (error)
        *error = file.errorString();
    return false;
}
