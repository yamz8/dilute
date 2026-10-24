#pragma once

#include <QQuickPaintedItem>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QPainter;

// A printable recipe card: what to make, when, and a row per thing to add,
// each with a box to tick off at the bench.
struct Card {
    QString title;
    QString subtitle;
    QStringList headers;
    QList<QStringList> rows;
    QStringList notes;

    static Card fromVariant(const QVariantMap &map);
    QVariantMap toVariant() const;
};

// One A5 page, in points. The preview, the PDF and the SVG all paint through
// paintCard(), so what's on screen is what prints.
constexpr double cardWidth = 419.53;
constexpr double cardHeight = 595.28;

void paintCard(QPainter &painter, const Card &card);
bool writeCardPdf(const QString &path, const Card &card, QString *error);
bool writeCardSvg(const QString &path, const Card &card, QString *error);

class CardView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QVariantMap card READ card WRITE setCard NOTIFY cardChanged)

public:
    explicit CardView(QQuickItem *parent = nullptr);

    QVariantMap card() const { return m_card; }
    void setCard(const QVariantMap &card);
    void paint(QPainter *painter) override;

signals:
    void cardChanged();

private:
    QVariantMap m_card;
};
