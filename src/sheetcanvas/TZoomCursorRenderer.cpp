// TZoomCursorRenderer.cpp
#include "TZoomCursorRenderer.h"
#include "TThemer.h"
#include <QPainterPath>
#include <QPointF>
#include <QLineF>

TZoomCursorRenderer::TZoomCursorRenderer(bool isVertical)
    : m_isVertical(isVertical)
{}

QRectF TZoomCursorRenderer::get_bounding_rect() const
{
    // SUBTLE BALANCE: Perfect intermediate sizing (100x80) to maximize space efficiency
    return QRectF(-50.0, -35.0, 100.0, 80.0);
}

void TZoomCursorRenderer::draw(QPainter* painter, const QVariant& liveData)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRectF bounds = get_bounding_rect();
    QColor themeColor = themer()->get_color("TrackPanel:text");
    if (!themeColor.isValid()) themeColor = QColor(225, 225, 225);

    // 1. Balanced LCD Display Area (Comfortable 20px height)
    const QRectF lcdRect(bounds.left() + 8.0, bounds.bottom() - 24.0, bounds.width() - 16.0, 20.0);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(12, 12, 12, 210));
    painter->drawRoundedRect(lcdRect, 3.5, 3.5);

    // Parse data tokens safely
    QString displayValue = liveData.isValid() ? liveData.toString() : (m_isVertical ? QStringLiteral("Vertical") : QStringLiteral("Horizontal"));

    QColor displayColor = themer()->get_color("Toolbar:icon-primary");
    if (!displayColor.isValid()) displayColor = QColor(255, 165, 0);

    painter->setPen(displayColor);
    // OPTIMIZED FONT: Set to 9pt Bold for clean, readable alphanumeric scaling
    painter->setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::Bold));
    painter->drawText(lcdRect, Qt::AlignCenter, displayValue);

    // 2. Elegant Vector Magnifying Glass Lens (Clean 2.0px pen stroke)
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(themeColor, 2.0, Qt::SolidLine, Qt::RoundCap));

    // Lens geometry centered comfortably at Y: -10.0
    QPointF lensCenter(0.0, -10.0);
    qreal radius = 12.0;
    painter->drawEllipse(lensCenter, radius, radius);

    painter->drawLine(QPointF(8.5, -1.5), QPointF(16.0, 6.0));

    // 3. Crisp Navigation Indicator Arrows (Subtle 1.8px pen stroke)
    painter->setPen(QPen(displayColor, 1.8, Qt::SolidLine, Qt::RoundCap));

    if (m_isVertical) {
        // Vertical double headed arrow geometry (Up / Down)
        painter->drawLine(QPointF(0.0, -17.0), QPointF(0.0, -3.0)); // Shaft
        // Top arrowhead
        painter->drawLine(QPointF(-4.0, -13.0), QPointF(0.0, -17.0));
        painter->drawLine(QPointF(4.0, -13.0), QPointF(0.0, -17.0));
        // Bottom arrowhead
        painter->drawLine(QPointF(-4.0, -7.0), QPointF(0.0, -3.0));
        painter->drawLine(QPointF(4.0, -7.0), QPointF(0.0, -3.0));
    } else {
        // Horizontal double headed arrow geometry (Left / Right)
        painter->drawLine(QPointF(-6.0, -10.0), QPointF(6.0, -10.0)); // Shaft
        // Left arrowhead
        painter->drawLine(QPointF(-2.5, -13.5), QPointF(-6.0, -10.0));
        painter->drawLine(QPointF(-2.5, -6.5), QPointF(-6.0, -10.0));
        // Right arrowhead
        painter->drawLine(QPointF(2.5, -13.5), QPointF(6.0, -10.0));
        painter->drawLine(QPointF(2.5, -6.5), QPointF(6.0, -10.0));
    }

    painter->restore();
}
