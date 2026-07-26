#pragma once

#include <QHash>
#include <QWidget>

class QVBoxLayout;
class QTimer;
class IndeterminateBar;

// Renders TaskManager's current task list live. Used by both TaskProgressPopup (the sidebar
// flyout) and ActivityTab (the full-page view), so the per-task row layout only exists once.
class TaskListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TaskListWidget(QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuild();
    void tickAnimation();

    QVBoxLayout *m_layout = nullptr;
    QTimer *m_animationTimer = nullptr;
    // Bars for tasks with no measurable progress (percent == -1) - refreshed on every
    // rebuild() and driven by m_animationTimer.
    QHash<int, IndeterminateBar *> m_indeterminateBars;
    qreal m_animationPhase = 0.0;
};
