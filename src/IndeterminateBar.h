#pragma once

#include <QWidget>

// progress bar for jobs with no real percent - a chunk slides left to right and loops.
// plain QProgressBar snapping 0->100->0 looks like the job keeps restarting, this doesn't.
class IndeterminateBar : public QWidget
{
    Q_OBJECT

public:
    explicit IndeterminateBar(QWidget *parent = nullptr);

    // 0 = chunk off-screen left, 1 = off-screen right
    void setPhase(qreal phase);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    qreal m_phase = 0.0;
};
