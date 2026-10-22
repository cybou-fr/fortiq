#pragma once
#include <QQuickPaintedItem>
#include <QFont>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <vterm.h>

class TerminalView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool inputEnabled MEMBER m_inputEnabled NOTIFY inputEnabledChanged)
    Q_PROPERTY(int columns READ columns NOTIFY dimensionsChanged)
    Q_PROPERTY(int rows READ rows NOTIFY dimensionsChanged)
public:
    explicit TerminalView(QQuickItem *parent = nullptr);
    int columns() const { return m_columns; }
    int rows() const { return m_rows; }
    ~TerminalView() override;
    void paint(QPainter *painter) override;
    Q_INVOKABLE void feed(const QByteArray &data);
    Q_INVOKABLE void reset();
    Q_INVOKABLE QString screenText() const;
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE void copyScreen();
    Q_INVOKABLE void paste(const QString &text);
    Q_INVOKABLE void insertHistory(const QString &text);
    Q_INVOKABLE void control(int code);
signals:
    void bytes(const QByteArray &data);
    void gridChanged(int columns, int rows);
    void pasteRequested();
    void inputEnabledChanged();
    void dimensionsChanged();
protected:
    void geometryChange(const QRectF &n, const QRectF &o) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
private:
    void sizeGrid();
    void textInput(const QString &text);
    bool terminalFocused() const;
    QTimer m_cursorTimer;
    bool m_cursorOn = true, m_cursorVisible = true;
    VTerm *m_vt;
    VTermScreen *m_screen;
    QFont m_font;
    qreal m_cellWidth = 8, m_cellHeight = 18, m_ascent = 13;
    int m_columns = 80, m_rows = 24;
    bool m_inputEnabled = false;
};
