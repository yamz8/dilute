#include "card.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFontMetricsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSvgGenerator>
#include <QTextOption>

#include "recipefile.h"

Card Card::fromVariant(const QVariantMap &map) {
    Card card;
    card.title = map.value("title").toString();
    card.subtitle = map.value("subtitle").toString();
    card.headers = map.value("headers").toStringList();
    for (const auto &row : map.value("rows").toList())
        card.rows << row.toStringList();
    card.notes = map.value("notes").toStringList();
    return card;
}

QVariantMap Card::toVariant() const {
    QVariantList list;
    for (const auto &row : rows)
        list << row;
    return {{"title", title}, {"subtitle", subtitle}, {"headers", headers}, {"rows", list}, {"notes", notes}};
}

namespace {
QFont font(double size, bool bold = false) {
    QFont f(QStringLiteral("sans-serif"));
    f.setPixelSize(qRound(size));
    f.setBold(bold);
    return f;
}
} // namespace

void paintCard(QPainter &painter, const Card &card) {
    const QColor ink(20, 20, 24), muted(110, 110, 118), rule(200, 200, 206);
    const double margin = 34;
    const double width = cardWidth - 2 * margin;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.fillRect(QRectF(0, 0, cardWidth, cardHeight), Qt::white);

    double y = margin;
    painter.setPen(ink);
    painter.setFont(font(20, true));
    const QFontMetricsF titleMetrics(painter.font());
    painter.drawText(QRectF(margin, y, width, titleMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                     titleMetrics.elidedText(card.title, Qt::ElideRight, width));
    y += titleMetrics.height() + 4;
    painter.setPen(muted);
    painter.setFont(font(10));
    const QFontMetricsF small(painter.font());
    painter.drawText(QRectF(margin, y, width, small.height()), Qt::AlignLeft | Qt::AlignVCenter,
                     small.elidedText(card.subtitle, Qt::ElideRight, width));
    y += small.height() + 14;

    // A box to tick, then the columns: the first takes what the others leave.
    const double box = 11, boxColumn = 24;
    const int columns = std::max<int>(1, card.headers.size());
    const QFont body = font(11), header = font(9, true), strong = font(11, true);
    QList<double> widths(columns, 0);
    for (int c = 1; c < columns; ++c) {
        widths[c] = QFontMetricsF(header).horizontalAdvance(card.headers.value(c)) + 14;
        for (const auto &row : card.rows)
            widths[c] = std::max(widths[c], QFontMetricsF(c == columns - 1 ? strong : body)
                                                    .horizontalAdvance(row.value(c)) + 14);
    }
    double others = 0;
    for (int c = 1; c < columns; ++c)
        others += widths[c];
    const double available = width - boxColumn;
    const double firstMinimum = available * 0.3;
    if (others > available - firstMinimum)
        for (int c = 1; c < columns; ++c)
            widths[c] *= (available - firstMinimum) / others;
    widths[0] = available - std::min(others, available - firstMinimum);

    auto columnX = [&](int c) {
        double x = margin + boxColumn;
        for (int i = 0; i < c; ++i)
            x += widths[i];
        return x;
    };
    auto align = [&](int c) { return (c == 0 ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter; };

    painter.setFont(header);
    painter.setPen(muted);
    const double headerHeight = QFontMetricsF(header).height() + 6;
    for (int c = 0; c < columns; ++c)
        painter.drawText(QRectF(columnX(c), y, widths[c] - (c ? 0 : 8), headerHeight), align(c),
                         card.headers.value(c).toUpper());
    y += headerHeight;
    painter.setPen(QPen(rule, 0.8));
    painter.drawLine(QPointF(margin, y), QPointF(margin + width, y));

    // Rows shrink to fit the page rather than spill off it.
    const double notesHeight = card.notes.isEmpty() ? 0 : 20 + card.notes.size() * 30;
    const double room = cardHeight - margin - 22 - notesHeight - y;
    const double rowHeight = card.rows.isEmpty() ? 26 : std::clamp(room / card.rows.size(), 14.0, 26.0);
    for (const auto &row : card.rows) {
        if (y + rowHeight > cardHeight - margin - 22)
            break;
        painter.setPen(QPen(ink, 0.9));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(margin + 1, y + (rowHeight - box) / 2, box, box));
        for (int c = 0; c < columns; ++c) {
            painter.setFont(c == columns - 1 && c > 0 ? strong : body);
            painter.setPen(c == 0 || c == columns - 1 ? ink : muted);
            const QFontMetricsF metrics(painter.font());
            const double w = widths[c] - (c ? 0 : 8);
            painter.drawText(QRectF(columnX(c), y, w, rowHeight), align(c),
                             metrics.elidedText(row.value(c), Qt::ElideRight, w));
        }
        y += rowHeight;
        painter.setPen(QPen(rule, 0.5));
        painter.drawLine(QPointF(margin, y), QPointF(margin + width, y));
    }

    if (!card.notes.isEmpty()) {
        y += 14;
        painter.setFont(font(10));
        painter.setPen(ink);
        QTextOption wrap;
        wrap.setWrapMode(QTextOption::WordWrap);
        for (const auto &note : card.notes) {
            const QRectF bounds = painter.boundingRect(QRectF(margin, y, width, 200), note, wrap);
            if (y + bounds.height() > cardHeight - margin - 16)
                break;
            painter.drawText(QRectF(margin, y, width, bounds.height()), note, wrap);
            y += bounds.height() + 4;
        }
    }

    painter.setFont(font(8));
    painter.setPen(muted);
    painter.drawText(QRectF(margin, cardHeight - margin - 10, width, 12), Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("dilute"));
    painter.restore();
}

bool writeCardPdf(const QString &path, const Card &card, QString *error) {
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    {
        // 72 dpi, so one device pixel is one point and the card's
        // coordinates land on the page unscaled.
        QPdfWriter pdf(&buffer);
        pdf.setPageSize(QPageSize(QPageSize::A5));
        pdf.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
        pdf.setResolution(72);
        pdf.setTitle(card.title);
        pdf.setCreator(QStringLiteral("dilute"));
        QPainter painter(&pdf);
        if (!painter.isActive()) {
            if (error)
                *error = QCoreApplication::translate("dilute", "Couldn't draw the PDF");
            return false;
        }
        paintCard(painter, card);
    }
    return writeAtomically(path, data, error);
}

bool writeCardSvg(const QString &path, const Card &card, QString *error) {
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    {
        QSvgGenerator svg;
        svg.setOutputDevice(&buffer);
        svg.setSize(QSize(qRound(cardWidth), qRound(cardHeight)));
        svg.setViewBox(QRectF(0, 0, cardWidth, cardHeight));
        svg.setResolution(72);
        svg.setTitle(card.title);
        QPainter painter(&svg);
        paintCard(painter, card);
    }
    return writeAtomically(path, data, error);
}

CardView::CardView(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
}

void CardView::setCard(const QVariantMap &card) {
    if (card == m_card)
        return;
    m_card = card;
    emit cardChanged();
    update();
}

void CardView::paint(QPainter *painter) {
    // Fit the page into the item, centered.
    const double scale = std::min(width() / cardWidth, height() / cardHeight);
    painter->save();
    painter->translate((width() - cardWidth * scale) / 2, (height() - cardHeight * scale) / 2);
    painter->scale(scale, scale);
    paintCard(*painter, Card::fromVariant(m_card));
    painter->restore();
}
