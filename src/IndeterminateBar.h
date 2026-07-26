#pragma once

#include <QWidget>

// A thin progress-bar-shaped widget for work with no measurable percent: a fixed-width
// "chunk" slides from the left edge to the right edge and loops, instead of a QProgressBar
// value filling from 0 up to 100 and snapping back (which reads as the operation restarting).
class IndeterminateBar : public QWidget
{
    Q_OBJECT

public:
    explicit IndeterminateBar(QWidget *parent = nullptr);

    // 0.0 = chunk fully off-screen to the left, 1.0 = chunk fully off-screen to the right.
    void setPhase(qreal phase);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    qreal m_phase = 0.0;
};
