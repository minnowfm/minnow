#include "IndeterminateBar.h"

#include <QPainter>
#include <QPainterPath>

IndeterminateBar::IndeterminateBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(6);
}

void IndeterminateBar::setPhase(qreal phase)
{
    m_phase = phase;
    update();
}

void IndeterminateBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    const qreal radius = height() / 2.0;
    const QRectF track(0, 0, width(), height());
    QPainterPath trackPath;
    trackPath.addRoundedRect(track, radius, radius);
    painter.fillPath(trackPath, palette().color(QPalette::Mid));

    painter.setClipPath(trackPath);
    const qreal chunkWidth = qMax(20.0, width() * 0.3);
    const qreal x = -chunkWidth + m_phase * (width() + chunkWidth);
    QPainterPath chunkPath;
    chunkPath.addRoundedRect(QRectF(x, 0, chunkWidth, height()), radius, radius);
    painter.fillPath(chunkPath, palette().color(QPalette::Highlight));
}
