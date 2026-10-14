#include "TDefaultCursorRenderer.h"
#include <QPainterPath>
#include <QLinearGradient>
#include <QFont>

TDefaultCursorRenderer::TDefaultCursorRenderer()
{
}

void TDefaultCursorRenderer::draw(QPainter *painter, const QVariant &liveData)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    qreal startX = - get_bounding_rect().width() / 2;
    qreal startY = - get_bounding_rect().height() / 2;

    int width = 13;
    int height = 20;
    int bottom = height + (height / 2) - 2;

    QPainterPath path;
    qreal halfWidth = width / 2.0;

    QPointF endPoint(startX + halfWidth + 1, startY + 1);
    QPointF c1(startX + 1, startY + height);
    QPointF c2(startX + width + 1, startY + height);

    path.moveTo(endPoint);
    path.quadTo(QPointF(startX - 1, startY + height * 0.75), c1);
    path.quadTo(QPointF(startX + halfWidth + 1, startY + bottom), c2);
    path.quadTo(QPointF(startX + width + 3, startY + height * 0.75), endPoint);

    QLinearGradient gradient;
    int graycolor = 180;
    int transparency = 230;
    QColor gray(graycolor, graycolor, graycolor, transparency);
    QColor black(0, 0, 0, transparency);

    gradient.setColorAt(0.0, gray);
    gradient.setColorAt(1.0, black);
    gradient.setStart(startX, startY);
    gradient.setFinalStop(startX, startY - height);
    gradient.setSpread(QGradient::ReflectSpread);

    painter->setBrush(gradient);
    int whiteValue = 230;
    painter->setPen(QColor(whiteValue, whiteValue, whiteValue));
    painter->drawPath(path);

    QString displayStr;
    if (liveData.canConvert<QChar>()) {
        displayStr = QString(liveData.value<QChar>());
    } else if (liveData.canConvert<QString>()) {
        displayStr = liveData.toString();
    }

    if (!displayStr.isEmpty()) {
        painter->setPen(QColor(Qt::yellow));
        QFont font;
        font.setPointSizeF(8);
        font.setKerning(false);
        painter->setFont(font);

        // Draw the letter centered in the drop shape of the cursor
        QRectF textRect(startX, startY + 11, width + 2, height - 11);
        painter->drawText(textRect, Qt::AlignCenter, displayStr);
    }

    painter->restore();
}

QRectF TDefaultCursorRenderer::get_bounding_rect() const
{
    // Enforce static boundary parameters for classical mouse arrows
    // Fits the traditional 13x20 bounding footprint natively
    return QRectF(-14, -20, 28, 40);
}
