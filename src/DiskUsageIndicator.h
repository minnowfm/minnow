#pragma once

#include <QWidget>

class QLabel;

// Shows a location's disk capacity/free space, either as plain text or as a compact
// colored fill-bar (color reflects how full the volume is - green/amber/red). Used inline
// per-drive in PlacesSidebar and for the optional root/home summary in MainWindow.
class DiskUsageIndicator : public QWidget
{
    Q_OBJECT

public:
    enum class Style { Text, Bar };

    explicit DiskUsageIndicator(QWidget *parent = nullptr);

    void setStyle(Style style);
    // Prefixes the Text-style line with a name, e.g. "Root (/) — 18.1 GiB free of 226.7 GiB"
    // instead of just the free/total figures. Ignored in Bar style. Empty by default.
    void setLabelText(const QString &text);
    // Queries QStorageInfo for `localPath` and updates the display. Safe to call again later
    // to refresh the numbers (e.g. after "Refresh Drives").
    void setPath(const QString &localPath);

    QSize sizeHint() const override;

    // Shared with PlacesSidebar's inline delegate-painted bars, so both use the same
    // green/amber/red thresholds.
    static QColor colorForPercentFree(double percentFree);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void rebuildDisplay();

    Style m_style = Style::Text;
    QString m_path;
    QString m_labelText;
    qint64 m_bytesTotal = 0;
    qint64 m_bytesAvailable = 0;
    QLabel *m_textLabel = nullptr;
};
