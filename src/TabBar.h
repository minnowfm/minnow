#pragma once

#include <QList>
#include <QWidget>

class QHBoxLayout;
class QLabel;

// A single tab button: title label + close button. Internal building block of TabBar.
class TabButton : public QWidget
{
    Q_OBJECT

public:
    explicit TabButton(const QString &text, QWidget *parent = nullptr);

    void setText(const QString &text);
    void setActive(bool active);

signals:
    void activated();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    QLabel *m_label = nullptr;
};

// A fully custom, self-drawn tab strip (no QTabBar/QTabWidget involved), so its layout,
// spacing, and active/hover styling are entirely under our own control. Hidden whenever
// it holds one tab or fewer. Pairs with a QStackedWidget that MainWindow keeps in sync
// by index (add/remove together, in the same order).
class TabBar : public QWidget
{
    Q_OBJECT

public:
    explicit TabBar(QWidget *parent = nullptr);

    int addTab(const QString &text);
    void setTabText(int index, const QString &text);
    void removeTab(int index);
    void setCurrentIndex(int index);
    int currentIndex() const { return m_currentIndex; }
    int count() const { return m_buttons.size(); }

signals:
    void currentChanged(int index);
    void tabCloseRequested(int index);

private:
    void updateVisibility();

    QHBoxLayout *m_layout = nullptr;
    QList<TabButton *> m_buttons;
    int m_currentIndex = -1;
};
