#include "TerminalView.h"
#include <QPainter>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QClipboard>
#include <QInputMethod>
#include <QKeyEvent>
#include <QMouseEvent>

TerminalView::TerminalView(QQuickItem *parent) : QQuickPaintedItem(parent), m_vt(vterm_new(24, 80)) {
    setFlag(ItemAcceptsInputMethod, true); setAcceptedMouseButtons(Qt::LeftButton);
    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont); m_font.setPixelSize(14);
    const QFontMetricsF fm(m_font); m_cellWidth = fm.horizontalAdvance('M'); m_cellHeight = fm.height(); m_ascent = fm.ascent();
    vterm_set_utf8(m_vt, 1); m_screen = vterm_obtain_screen(m_vt);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_output_set_callback(m_vt, [](const char *s, size_t n, void *p) {
        auto *view = static_cast<TerminalView *>(p);
        if (view->m_inputEnabled) emit view->bytes(QByteArray(s, n));
    }, this);
    reset();
    connect(this, &QQuickItem::activeFocusChanged, this, [this] { update(); });
}
TerminalView::~TerminalView() { vterm_free(m_vt); }
void TerminalView::reset() {
    vterm_screen_reset(m_screen, 1);
    VTermColor fg, bg; vterm_color_rgb(&fg, 235, 241, 252); vterm_color_rgb(&bg, 8, 16, 28);
    vterm_screen_set_default_colors(m_screen, &fg, &bg); update();
}
void TerminalView::feed(const QByteArray &data) {
    vterm_input_write(m_vt, data.constData(), data.size()); vterm_screen_flush_damage(m_screen); update();
}
void TerminalView::geometryChange(const QRectF &n, const QRectF &o) { QQuickPaintedItem::geometryChange(n, o); sizeGrid(); }
void TerminalView::sizeGrid() {
    const int columns = qBound(2, int(width() / m_cellWidth), 240), rows = qBound(2, int(height() / m_cellHeight), 100);
    if (columns == m_columns && rows == m_rows) return;
    m_columns = columns; m_rows = rows; vterm_set_size(m_vt, rows, columns); update(); emit dimensionsChanged(); emit gridChanged(columns, rows);
}
void TerminalView::paint(QPainter *p) {
    p->fillRect(boundingRect(), QColor(8,16,28,235));
    for (int row = 0; row < m_rows; ++row) for (int col = 0; col < m_columns; ++col) {
        VTermScreenCell cell{}; if (!vterm_screen_get_cell(m_screen, {row, col}, &cell) || cell.width == 0) continue;
        auto fg = cell.fg, bg = cell.bg;
        vterm_screen_convert_color_to_rgb(m_screen, &fg); vterm_screen_convert_color_to_rgb(m_screen, &bg);
        QColor f(fg.rgb.red, fg.rgb.green, fg.rgb.blue), b(bg.rgb.red, bg.rgb.green, bg.rgb.blue);
        if (cell.attrs.reverse) std::swap(f,b);
        if (!VTERM_COLOR_IS_DEFAULT_BG(&cell.bg) || cell.attrs.reverse)
            p->fillRect(QRectF(col*m_cellWidth,row*m_cellHeight,cell.width*m_cellWidth,m_cellHeight),b);
        if (cell.chars[0] && cell.chars[0] != UINT32_MAX && !cell.attrs.conceal) {
            QString text; for (uint32_t cp : cell.chars) { if (!cp || cp == UINT32_MAX) break; const char32_t c = cp; text += QString::fromUcs4(&c,1); }
            auto font = m_font; font.setBold(cell.attrs.bold); font.setItalic(cell.attrs.italic); font.setUnderline(cell.attrs.underline);
            p->setFont(font); p->setPen(f); p->drawText(QPointF(col*m_cellWidth,row*m_cellHeight+m_ascent),text);
        }
    }
    if (hasActiveFocus() && m_inputEnabled) {
        VTermPos pos; vterm_state_get_cursorpos(vterm_obtain_state(m_vt), &pos);
        p->setPen(QColor("#8DBEFF")); p->drawRect(QRectF(pos.col*m_cellWidth,pos.row*m_cellHeight,m_cellWidth,m_cellHeight));
    }
}
QString TerminalView::screenText() const {
    QByteArray text(m_rows*m_columns*24, Qt::Uninitialized);
    text.resize(vterm_screen_get_text(m_screen,text.data(),text.size(),{0,m_rows,0,m_columns})); return QString::fromUtf8(text);
}
QString TerminalView::clipboardText() const { return QGuiApplication::clipboard()->text(); }
void TerminalView::copyScreen() { QGuiApplication::clipboard()->setText(screenText()); }
void TerminalView::paste(const QString &text) {
    if (!m_inputEnabled || text.toUtf8().size() > 16000 || text.contains(QChar::Null)) return;
    vterm_keyboard_start_paste(m_vt); textInput(text); vterm_keyboard_end_paste(m_vt);
}
void TerminalView::control(int code) { if (m_inputEnabled && code >= 1 && code <= 26) emit bytes(QByteArray(1,char(code))); }
void TerminalView::textInput(const QString &text) { for (auto cp : text.toUcs4()) vterm_keyboard_unichar(m_vt, cp, VTERM_MOD_NONE); }
void TerminalView::keyPressEvent(QKeyEvent *e) {
    if (!m_inputEnabled) { e->ignore(); return; }
    const auto mods = e->modifiers();
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) && e->key() == Qt::Key_C) { copyScreen(); return; }
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) && e->key() == Qt::Key_V) { emit pasteRequested(); return; }
    int key = VTERM_KEY_NONE;
    switch (e->key()) {
    case Qt::Key_Return: case Qt::Key_Enter: key=VTERM_KEY_ENTER; break;
    case Qt::Key_Tab: key=VTERM_KEY_TAB; break; case Qt::Key_Backspace: key=VTERM_KEY_BACKSPACE; break;
    case Qt::Key_Escape:key=VTERM_KEY_ESCAPE;break;case Qt::Key_Up:key=VTERM_KEY_UP;break;
    case Qt::Key_Down:key=VTERM_KEY_DOWN;break;case Qt::Key_Left:key=VTERM_KEY_LEFT;break;case Qt::Key_Right:key=VTERM_KEY_RIGHT;break;
    case Qt::Key_Home:key=VTERM_KEY_HOME;break;case Qt::Key_End:key=VTERM_KEY_END;break;
    case Qt::Key_Delete:key=VTERM_KEY_DEL;break;case Qt::Key_Insert:key=VTERM_KEY_INS;break;
    case Qt::Key_PageUp:key=VTERM_KEY_PAGEUP;break;case Qt::Key_PageDown:key=VTERM_KEY_PAGEDOWN;break;
    default: if(e->key()>=Qt::Key_F1 && e->key()<=Qt::Key_F12)key=VTERM_KEY_FUNCTION(e->key()-Qt::Key_F1+1);
    }
    const auto mod = VTermModifier(((mods & Qt::ShiftModifier)?VTERM_MOD_SHIFT:0)|((mods & Qt::AltModifier)?VTERM_MOD_ALT:0)|((mods & Qt::ControlModifier)?VTERM_MOD_CTRL:0));
    if (key) vterm_keyboard_key(m_vt,VTermKey(key),mod);
    else if ((mods & Qt::ControlModifier) && e->key()>=Qt::Key_A && e->key()<=Qt::Key_Z) vterm_keyboard_unichar(m_vt,e->key()-Qt::Key_A+'a',mod);
    else for(auto cp:e->text().toUcs4())vterm_keyboard_unichar(m_vt,cp,mod);
    e->accept();
}
void TerminalView::mousePressEvent(QMouseEvent *e) { forceActiveFocus(); if(m_inputEnabled)QGuiApplication::inputMethod()->show();update();e->accept(); }
void TerminalView::inputMethodEvent(QInputMethodEvent *e) { if(m_inputEnabled)textInput(e->commitString()); e->accept(); }
QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery q) const {
    if(q==Qt::ImEnabled)return m_inputEnabled;
    if(q==Qt::ImHints)return int(Qt::ImhNoPredictiveText|Qt::ImhNoAutoUppercase|Qt::ImhSensitiveData);
    if(q==Qt::ImCursorRectangle){ VTermPos p;vterm_state_get_cursorpos(vterm_obtain_state(m_vt),&p);return QRectF(p.col*m_cellWidth,p.row*m_cellHeight,m_cellWidth,m_cellHeight); }
    return QQuickPaintedItem::inputMethodQuery(q);
}
