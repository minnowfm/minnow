#include "DiskUsageIndicator.h"

#include <KIO/Global>

#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QStorageInfo>
#include <QVBoxLayout>

DiskUsageIndicator::DiskUsageIndicator(QWidget *parent)
    : QWidget(parent)
{
    // layout margins default to the widget's own contentsMargins, so setContentsMargins() on
    // the indicator itself indents both the text label and (via paintEvent's contentsRect()) the bar
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(0);

    m_textLabel = new QLabel(this);
    m_textLabel->setWordWrap(false);
    QFont font = m_textLabel->font();
    font.setPointSizeF(font.pointSizeF() * 0.85);
    m_textLabel->setFont(font);
    layout->addWidget(m_textLabel);
}

void DiskUsageIndicator::setStyle(Style style)
{
    if (m_style == style)
        return;
    m_style = style;
    rebuildDisplay();
}

void DiskUsageIndicator::setLabelText(const QString &text)
{
    if (m_labelText == text)
        return;
    m_labelText = text;
    rebuildDisplay();
}

void DiskUsageIndicator::setPath(const QString &localPath)
{
    m_path = localPath;
    const QStorageInfo info(localPath);
    m_bytesTotal = info.bytesTotal();
    m_bytesAvailable = info.bytesAvailable();
    rebuildDisplay();
}

void DiskUsageIndicator::rebuildDisplay()
{
    QString text;
    if (m_bytesTotal > 0) {
        const QString usage = tr("%1 free of %2").arg(KIO::convertSize(m_bytesAvailable), KIO::convertSize(m_bytesTotal));
        text = m_labelText.isEmpty() ? usage : tr("%1 — %2").arg(m_labelText, usage);
    }

    if (m_style == Style::Bar) {
        m_textLabel->hide();
        const QMargins margins = contentsMargins();
        setFixedHeight(6 + margins.top() + margins.bottom());
        setToolTip(text);
    } else {
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        m_textLabel->setText(text);
        m_textLabel->show();
        setToolTip(QString());
    }
    updateGeometry();
    update();
}

QColor DiskUsageIndicator::colorForPercentFree(double percentFree)
{
    if (percentFree < 10.0)
        return QColor(220, 70, 70); // critical
    if (percentFree < 20.0)
        return QColor(230, 165, 45); // getting full
    return QColor(75, 175, 95); // plenty free
}

QSize DiskUsageIndicator::sizeHint() const
{
    if (m_style == Style::Bar) {
        const QMargins margins = contentsMargins();
        return QSize(120 + margins.left() + margins.right(), 6 + margins.top() + margins.bottom());
    }
    return QWidget::sizeHint();
}

void DiskUsageIndicator::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    if (m_style != Style::Bar || m_bytesTotal <= 0)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    const QRectF track = contentsRect();
    const qreal radius = track.height() / 2.0;
    QPainterPath trackPath;
    trackPath.addRoundedRect(track, radius, radius);
    painter.fillPath(trackPath, palette().color(QPalette::Mid));

    const double percentFree = m_bytesAvailable * 100.0 / m_bytesTotal;
    const double usedFraction = 1.0 - (percentFree / 100.0);
    const qreal fillWidth = qBound(0.0, track.width() * usedFraction, track.width());
    if (fillWidth <= 0)
        return;

    painter.setClipPath(trackPath);
    QPainterPath fillPath;
    fillPath.addRoundedRect(QRectF(track.left(), track.top(), fillWidth, track.height()), radius, radius);
    painter.fillPath(fillPath, colorForPercentFree(percentFree));
}
