#pragma once
#include <QQuickPaintedItem>
#include <QFont>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <vterm.h>
#include <deque>
#include <vector>

class TerminalView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool inputEnabled MEMBER m_inputEnabled NOTIFY inputEnabledChanged)
    Q_PROPERTY(int columns READ columns NOTIFY dimensionsChanged)
    Q_PROPERTY(int rows READ rows NOTIFY dimensionsChanged)
    Q_PROPERTY(int scrollbackLines READ scrollbackLines NOTIFY scrollbackChanged)
    Q_PROPERTY(int scrollOffset READ scrollOffset NOTIFY scrollbackChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QString searchStatus READ searchStatus NOTIFY searchChanged)
    Q_PROPERTY(int backlog READ backlog NOTIFY backlogChanged)   // bytes received but not yet interpreted (feedLater)
public:
    explicit TerminalView(QQuickItem *parent = nullptr);
    int columns() const { return m_columns; }
    int rows() const { return m_rows; }
    int scrollbackLines() const { return m_alternateScreen ? 0 : int(m_scrollback.size()); }
    int scrollOffset() const { return m_scrollOffset; }
    int backlog() const { return int(m_queue.size() - m_queueOffset); }
    bool hasSelection() const { return m_selectionStart >= 0 && m_selectionEnd >= 0; }
    QString searchStatus() const { return m_searchStatus; }
    ~TerminalView() override;
    void paint(QPainter *painter) override;
    Q_INVOKABLE void feed(const QByteArray &data);
    // Same as feed(), but the bytes are interpreted in short slices between event-loop turns (about 8 ms of work each), so a flood of
    // short lines cannot freeze the window; the order is kept. The owner should stop reading while backlog() is large (backpressure).
    Q_INVOKABLE void feedLater(const QByteArray &data);
    Q_INVOKABLE void reset();
    Q_INVOKABLE QString screenText() const;
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE void copyScreen();
    Q_INVOKABLE QString selectedText() const;
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void findText(const QString &query, bool backward);
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE void paste(const QString &text);
    Q_INVOKABLE void insertHistory(const QString &text);
    Q_INVOKABLE void control(int code);
    Q_INVOKABLE void scrollTo(int offset);
signals:
    void bytes(const QByteArray &data);
    void gridChanged(int columns, int rows);
    void pasteRequested();
    void inputEnabledChanged();
    void dimensionsChanged();
    void scrollbackChanged();
    void selectionChanged();
    void searchChanged();
    void searchRequested();
    void backlogChanged();
protected:
    void geometryChange(const QRectF &n, const QRectF &o) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
private:
    void sizeGrid();
    void drainQueue();
    QByteArray m_queue;
    qsizetype m_queueOffset = 0;
    bool m_drainScheduled = false;
    void textInput(const QString &text);
    bool terminalFocused() const;
    bool viewportCell(int row, int col, VTermScreenCell *cell) const;
    bool bufferCell(int row, int col, VTermScreenCell *cell) const;
    void invalidateSearch();
    QString m_searchQuery, m_searchStatus;
    int m_searchCurrent = -1;
    int cellIndex(const QPointF &position) const;
    void extendSelection(const QPointF &position);
    QString textRange(int first, int last) const;
    int m_selectionStart = -1, m_selectionEnd = -1, m_selectionAnchor = -1;
    std::deque<std::vector<VTermScreenCell>> m_scrollback;
    static constexpr int ScrollbackLimit = 1000;
    int m_scrollOffset = 0;
    qreal m_wheelRemainder = 0;
    bool m_alternateScreen = false;
    QTimer m_cursorTimer;
    bool m_cursorOn = true, m_cursorVisible = true;
    VTerm *m_vt;
    VTermScreen *m_screen;
    QFont m_font;
    qreal m_cellWidth = 8, m_cellHeight = 18, m_ascent = 13;
    int m_columns = 80, m_rows = 24;
    bool m_inputEnabled = false;
};
