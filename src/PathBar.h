#pragma once

#include <QUrl>
#include <QWidget>

class QHBoxLayout;
class QLineEdit;
class QStackedLayout;
class QDropEvent;
class QDragEnterEvent;
class QDragMoveEvent;

// A Dolphin-style breadcrumb path bar: clickable path segments, and clicking
// anywhere past the last segment switches to a plain editable line for typing a path.
class PathBar : public QWidget
{
    Q_OBJECT

public:
    explicit PathBar(QWidget *parent = nullptr);

    void setUrl(const QUrl &url);
    QUrl url() const { return m_url; }

signals:
    void urlActivated(const QUrl &url);
    void urlsDropped(const QUrl &destination, QDropEvent *event);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void rebuildSegments();
    void enterEditMode();
    void commitEdit();
    void cancelEdit();

    QUrl m_url;
    QStackedLayout *m_stack = nullptr;
    QWidget *m_breadcrumbWidget = nullptr;
    QHBoxLayout *m_breadcrumbLayout = nullptr;
    QLineEdit *m_editLine = nullptr;
};
