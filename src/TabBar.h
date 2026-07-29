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

// custom-drawn tab strip, not QTabBar - needed full control over spacing/hover/active look.
// auto-hides at <=1 tab. MainWindow keeps a QStackedWidget in sync with this by index.
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
