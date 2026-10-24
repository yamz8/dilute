#pragma once

#include <QStringList>

// Commands for scripts and agents that need no display: dilute c1v1, mass,
// serial, recipe, convert and skill. Follows Hype's cli.cpp.
bool isCliCommand(const QString &word);
QString cliSummary();
int runCli(const QStringList &arguments);
