#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include "calc.h"

// A .dilute file is JSON: {"format": "dilute", "version": 1, "name", "notes",
// "volume": "500 mL", "components": [{"name", "mw", "final": "137 mM",
// "stock": "5 M"}]}. Quantities are written as text with a C-locale number,
// so the file reads the same everywhere and by hand.
namespace RecipeFile {
QJsonObject toJson(const Recipe &recipe);
bool fromJson(const QJsonObject &object, Recipe *recipe, QString *error);
bool load(const QString &path, Recipe *recipe, QString *error);
bool save(const QString &path, const Recipe &recipe, QString *error);
} // namespace RecipeFile

// Writes beside the target and renames over it, so a crash or a full disk
// never leaves half a file.
bool writeAtomically(const QString &path, const QByteArray &data, QString *error);
