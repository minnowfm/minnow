#pragma once

#include <QHash>
#include <QWidget>

class QVBoxLayout;
class QTimer;
class IndeterminateBar;

// live render of TaskManager's task list, shared by the popup flyout and the full ActivityTab
// so we don't have two copies of the row layout
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
    QHash<int, IndeterminateBar *> m_indeterminateBars; // percent == -1 tasks, rebuilt each rebuild()
    qreal m_animationPhase = 0.0;
};
