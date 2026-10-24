#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include "calc.h"

class FilePicker;

// The calculators, as the window sees them: each takes the form's text and
// units and answers with everything to show, already formatted for the
// Omarchy language. Also keeps the settings, the history of recent
// calculations and the editable reagent list.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(int significantFigures READ significantFigures WRITE setSignificantFigures NOTIFY settingsChanged)
    Q_PROPERTY(double minimumPipette READ minimumPipette WRITE setMinimumPipette NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QVariantList reagents READ reagents NOTIFY reagentsChanged)

public:
    struct Paths {
        QString config, data, state;
        static Paths standard();
    };

    explicit Backend(FilePicker *picker, const Paths &paths = Paths::standard(), QObject *parent = nullptr);

    const Settings &settings() const { return m_settings; }
    const Paths &paths() const { return m_paths; }
    FilePicker *picker() const { return m_picker; }

    int significantFigures() const { return m_settings.significantFigures; }
    void setSignificantFigures(int figures);
    // In µL, as the window shows it.
    double minimumPipette() const { return m_settings.minimumPipetteLiters * 1e6; }
    void setMinimumPipette(double microliters);

    QVariantList history() const { return m_history; }
    QVariantList reagents() const;

    Q_INVOKABLE QStringList units(const QString &kind) const;
    Q_INVOKABLE QVariantMap dilution(const QVariantMap &inputs) const;
    Q_INVOKABLE QVariantMap molarity(const QVariantMap &inputs) const;
    Q_INVOKABLE QVariantMap serial(const QVariantMap &inputs) const;
    Q_INVOKABLE QString number(double value) const;

    Q_INVOKABLE void copy(const QString &text);
    Q_INVOKABLE void remember(int tab, const QVariantMap &inputs, const QString &summary);
    Q_INVOKABLE void clearHistory();

    Q_INVOKABLE void setReagent(int index, const QString &name, const QString &mw);
    Q_INVOKABLE void addReagent();
    Q_INVOKABLE void removeReagent(int index);
    Q_INVOKABLE void resetReagents();

    // Asks where, then writes the card as "pdf" or "svg".
    Q_INVOKABLE void exportCard(const QVariantMap &card, const QString &format);
    // Saves a calculation as a .dilute recipe (in its JSON form), asking where.
    Q_INVOKABLE void saveRecipe(const QVariantMap &recipe);
    bool writeCard(const QString &path, const QVariantMap &card, const QString &format, QString *error);

    void notify(const QString &text, bool error = false) { emit message(text, error); }
    static QString today();

signals:
    void settingsChanged();
    void historyChanged();
    void reagentsChanged();
    void message(const QString &text, bool error);

private:
    void saveSettings();
    void saveHistory();
    void saveReagents();

    FilePicker *m_picker;
    Paths m_paths;
    Settings m_settings;
    QVariantList m_history;
    QList<Reagent> m_reagents;
};
