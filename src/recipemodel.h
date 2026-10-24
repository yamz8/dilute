#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QVariantMap>

#include "calc.h"

class Backend;

// The buffer recipe being edited. It keeps the text as typed, so a field
// never reformats under the cursor, and works out every amount on each
// change. Edits can be undone, and the recipe saves as a .dilute file.
class RecipeModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(QString notes READ notes WRITE setNotes NOTIFY changed)
    Q_PROPERTY(QString volumeText READ volumeText NOTIFY changed)
    Q_PROPERTY(QString volumeUnit READ volumeUnit NOTIFY changed)
    Q_PROPERTY(QVariantMap result READ result NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(QString fileName READ fileName NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QStringList presets READ presets NOTIFY presetsChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        MwRole,
        FinalTextRole,
        FinalUnitRole,
        StockTextRole,
        StockUnitRole,
        AmountRole,
        IsVolumeRole,
        ErrorRole,
        WarningRole,
    };

    struct Row {
        QString name, mw, finalText, finalUnit = "mM", stockText, stockUnit = "M";
        bool operator==(const Row &) const = default;
    };
    struct Draft {
        QString name, notes, volumeText = "1", volumeUnit = "L";
        QList<Row> rows;
        bool operator==(const Draft &) const = default;
    };

    explicit RecipeModel(Backend *backend, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString name() const { return m_draft.name; }
    QString notes() const { return m_draft.notes; }
    QString volumeText() const { return m_draft.volumeText; }
    QString volumeUnit() const { return m_draft.volumeUnit; }
    void setName(const QString &name);
    void setNotes(const QString &notes);
    QVariantMap result() const { return m_result; }
    bool dirty() const { return m_draft != m_saved; }
    QString fileName() const;
    QString filePath() const { return m_path; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QStringList presets() const;

    Q_INVOKABLE void setVolume(const QString &text, const QString &unit);
    Q_INVOKABLE void setField(int row, const QString &field, const QString &value);
    Q_INVOKABLE void addComponent();
    Q_INVOKABLE void removeComponent(int row);
    Q_INVOKABLE void loadPreset(const QString &name);
    Q_INVOKABLE void newRecipe();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Works out every amount again; also after a language or settings change.
    Q_INVOKABLE void recompute();

    Q_INVOKABLE void save();
    Q_INVOKABLE void saveAs();
    Q_INVOKABLE void open();
    Q_INVOKABLE void savePreset();
    Q_INVOKABLE QVariantMap snapshot() const;
    Q_INVOKABLE void restore(const QVariantMap &recipe);
    bool openPath(const QString &path);
    bool saveTo(const QString &path);

    // The draft as a recipe, and per row what kept it from parsing.
    Recipe recipe(QStringList *rowErrors = nullptr) const;
    void setRecipe(const Recipe &recipe, const QString &path = {});

signals:
    void changed();
    void presetsChanged();

private:
    void edit(const QString &key, const std::function<void()> &change);
    void replace(const Draft &draft);
    QString presetDirectory() const;

    Backend *m_backend;
    Draft m_draft, m_saved;
    QString m_path;
    QList<Draft> m_undo, m_redo;
    QString m_lastEdit;
    QElapsedTimer m_sinceEdit;
    QList<ComponentAmount> m_amounts;
    QStringList m_rowErrors;
    QVariantMap m_result;
};
