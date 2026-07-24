#include "PathBar.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QStackedLayout>
#include <QToolButton>

PathBar::PathBar(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setAcceptDrops(true);

    m_breadcrumbWidget = new QWidget(this);
    m_breadcrumbLayout = new QHBoxLayout(m_breadcrumbWidget);
    m_breadcrumbLayout->setContentsMargins(4, 0, 4, 0);
    m_breadcrumbLayout->setSpacing(2);
    m_breadcrumbWidget->installEventFilter(this);

    m_editLine = new QLineEdit(this);
    m_editLine->setFrame(false);
    connect(m_editLine, &QLineEdit::returnPressed, this, &PathBar::commitEdit);
    m_editLine->installEventFilter(this);

    m_stack = new QStackedLayout(this);
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->addWidget(m_breadcrumbWidget);
    m_stack->addWidget(m_editLine);
    m_stack->setCurrentWidget(m_breadcrumbWidget);
}

void PathBar::setUrl(const QUrl &url)
{
    m_url = url;
    rebuildSegments();
    if (m_stack->currentWidget() != m_breadcrumbWidget)
        m_stack->setCurrentWidget(m_breadcrumbWidget);
}

void PathBar::rebuildSegments()
{
    QLayoutItem *item = nullptr;
    while ((item = m_breadcrumbLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    auto addSegment = [this](const QString &label, const QUrl &target, bool isLast) {
        auto *button = new QToolButton(m_breadcrumbWidget);
        button->setText(label);
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        if (isLast) {
            QFont font = button->font();
            font.setBold(true);
            button->setFont(font);
        }
        connect(button, &QToolButton::clicked, this, [this, target] { Q_EMIT urlActivated(target); });
        m_breadcrumbLayout->addWidget(button);
        return button;
    };

    const QStringList parts = m_url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);

    QUrl rootUrl = m_url;
    rootUrl.setPath(QStringLiteral("/"));
    addSegment(QStringLiteral("/"), rootUrl, parts.isEmpty());

    QString accumulated;
    for (int i = 0; i < parts.size(); ++i) {
        auto *separator = new QLabel(QStringLiteral("›"), m_breadcrumbWidget);
        separator->setEnabled(false);
        m_breadcrumbLayout->addWidget(separator);

        accumulated += QLatin1Char('/') + parts.at(i);
        QUrl segmentUrl = m_url;
        segmentUrl.setPath(accumulated);
        addSegment(parts.at(i), segmentUrl, i == parts.size() - 1);
    }

    m_breadcrumbLayout->addStretch(1);
}

void PathBar::enterEditMode()
{
    m_editLine->setText(m_url.toDisplayString(QUrl::PreferLocalFile));
    m_stack->setCurrentWidget(m_editLine);
    m_editLine->setFocus(Qt::MouseFocusReason);
    m_editLine->selectAll();
}

void PathBar::commitEdit()
{
    const QString text = m_editLine->text().trimmed();
    if (!text.isEmpty()) {
        const QUrl url = QUrl::fromUserInput(text);
        if (url.isValid()) {
            Q_EMIT urlActivated(url);
            return;
        }
    }
    m_stack->setCurrentWidget(m_breadcrumbWidget);
}

void PathBar::cancelEdit()
{
    if (m_stack->currentWidget() == m_editLine)
        m_stack->setCurrentWidget(m_breadcrumbWidget);
}

bool PathBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_breadcrumbWidget && event->type() == QEvent::MouseButtonPress) {
        enterEditMode();
        return true;
    }
    if (watched == m_editLine && event->type() == QEvent::FocusOut) {
        cancelEdit();
    }
    return QWidget::eventFilter(watched, event);
}

void PathBar::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        event->ignore();
}

void PathBar::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        event->ignore();
}

void PathBar::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    Q_EMIT urlsDropped(m_url, event);
    event->acceptProposedAction();
}
