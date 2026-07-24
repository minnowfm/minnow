#include "TabBar.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QStyle>
#include <QToolButton>

TabButton::TabButton(const QString &text, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("tabButton"));
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::PointingHandCursor);
    setProperty("active", false);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 6, 8, 6);
    layout->setSpacing(8);

    m_label = new QLabel(text, this);
    layout->addWidget(m_label);

    auto *closeButton = new QToolButton(this);
    closeButton->setObjectName(QStringLiteral("tabCloseButton"));
    closeButton->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    closeButton->setAutoRaise(true);
    closeButton->setFixedSize(18, 18);
    closeButton->setIconSize(QSize(9, 9));
    closeButton->setCursor(Qt::ArrowCursor);
    layout->addWidget(closeButton);

    connect(closeButton, &QToolButton::clicked, this, &TabButton::closeRequested);
}

void TabButton::setText(const QString &text)
{
    m_label->setText(text);
}

void TabButton::setActive(bool active)
{
    setProperty("active", active);
    style()->unpolish(this);
    style()->polish(this);
}

void TabButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        Q_EMIT activated();
    QWidget::mousePressEvent(event);
}

TabBar::TabBar(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(2);
    m_layout->addStretch(1);
    updateVisibility();
}

int TabBar::addTab(const QString &text)
{
    auto *button = new TabButton(text, this);
    connect(button, &TabButton::activated, this, [this, button] {
        const int index = m_buttons.indexOf(button);
        if (index >= 0)
            setCurrentIndex(index);
    });
    connect(button, &TabButton::closeRequested, this, [this, button] {
        const int index = m_buttons.indexOf(button);
        if (index >= 0)
            Q_EMIT tabCloseRequested(index);
    });

    const int insertPos = m_buttons.size();
    m_layout->insertWidget(insertPos, button);
    m_buttons.append(button);
    updateVisibility();
    return insertPos;
}

void TabBar::setTabText(int index, const QString &text)
{
    if (index >= 0 && index < m_buttons.size())
        m_buttons.at(index)->setText(text);
}

void TabBar::removeTab(int index)
{
    if (index < 0 || index >= m_buttons.size())
        return;

    TabButton *button = m_buttons.takeAt(index);
    m_layout->removeWidget(button);
    button->deleteLater();

    if (m_currentIndex == index)
        m_currentIndex = -1;
    else if (m_currentIndex > index)
        --m_currentIndex;

    updateVisibility();
}

void TabBar::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_buttons.size() || index == m_currentIndex)
        return;

    if (m_currentIndex >= 0 && m_currentIndex < m_buttons.size())
        m_buttons.at(m_currentIndex)->setActive(false);

    m_currentIndex = index;
    m_buttons.at(m_currentIndex)->setActive(true);
    Q_EMIT currentChanged(m_currentIndex);
}

void TabBar::updateVisibility()
{
    setVisible(m_buttons.size() > 1);
}
