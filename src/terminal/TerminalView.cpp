#include "TerminalView.h"
#include <QPainter>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QClipboard>
#include <QInputMethod>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QWheelEvent>
#include <algorithm>

TerminalView::TerminalView(QQuickItem *parent) : QQuickPaintedItem(parent), m_vt(vterm_new(24, 80)) {
    setFlag(ItemAcceptsInputMethod, true); setAcceptedMouseButtons(Qt::LeftButton);
    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont); m_font.setPixelSize(14);
    const QFontMetricsF fm(m_font); m_cellWidth = fm.horizontalAdvance('M'); m_cellHeight = fm.height(); m_ascent = fm.ascent();
    vterm_set_utf8(m_vt, 1); m_screen = vterm_obtain_screen(m_vt);
    vterm_screen_enable_altscreen(m_screen, 1);
    static const VTermScreenCallbacks callbacks = [] {
        VTermScreenCallbacks cb{};
        cb.settermprop = [](VTermProp prop, VTermValue *value, void *user) {
            auto *view = static_cast<TerminalView *>(user);
            if (prop == VTERM_PROP_CURSORVISIBLE) view->m_cursorVisible = value->boolean;
            if (prop == VTERM_PROP_ALTSCREEN) {
                view->m_alternateScreen = value->boolean;
                view->m_scrollOffset = 0;
            }
            return 1;
        };
        cb.sb_pushline = [](int cols, const VTermScreenCell *cells, void *user) {
            auto *view = static_cast<TerminalView *>(user);
            view->m_scrollback.emplace_back(cells, cells + cols);
            if (view->m_scrollOffset) ++view->m_scrollOffset;
            if (view->m_scrollback.size() > ScrollbackLimit) view->m_scrollback.pop_front();
            view->m_scrollOffset = std::min(view->m_scrollOffset, int(view->m_scrollback.size()));
            return 1;
        };
        cb.sb_popline = [](int cols, VTermScreenCell *cells, void *user) {
            auto *view = static_cast<TerminalView *>(user);
            if (view->m_scrollback.empty()) return 0;
            const auto &line = view->m_scrollback.back();
            VTermScreenCell blank{}; blank.width = 1;
            vterm_state_get_default_colors(vterm_obtain_state(view->m_vt), &blank.fg, &blank.bg);
            std::fill_n(cells, cols, blank);
            std::copy_n(line.begin(), std::min(cols, int(line.size())), cells);
            view->m_scrollback.pop_back();
            view->m_scrollOffset = std::min(view->m_scrollOffset, int(view->m_scrollback.size()));
            return 1;
        };
        cb.sb_clear = [](void *user) {
            auto *view = static_cast<TerminalView *>(user);
            view->m_scrollback.clear(); view->m_scrollOffset = 0;
            return 1;
        };
        return cb;
    }();
    vterm_screen_set_callbacks(m_screen, &callbacks, this);
    vterm_output_set_callback(m_vt, [](const char *s, size_t n, void *p) {
        auto *view = static_cast<TerminalView *>(p);
        if (view->m_inputEnabled) emit view->bytes(QByteArray(s, n));
    }, this);
    reset();
    m_cursorTimer.setInterval(500);
    connect(&m_cursorTimer, &QTimer::timeout, this, [this] {
        if (isVisible() && m_inputEnabled) { m_cursorOn = !m_cursorOn; update(); }
    });
    m_cursorTimer.start();
    connect(this, &QQuickItem::activeFocusChanged, this, [this] { m_cursorOn = true; update(); });
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *w) {
        if (w) connect(w, &QWindow::activeChanged, this, [this] { m_cursorOn = true; update(); });
    });
}
bool TerminalView::terminalFocused() const { return hasActiveFocus() && window() && window()->isActive(); }
TerminalView::~TerminalView() { vterm_free(m_vt); }
void TerminalView::reset() {
    clearSelection();
    m_scrollback.clear(); m_scrollOffset = 0; m_wheelRemainder = 0;
    vterm_screen_reset(m_screen, 1);
    VTermColor fg, bg; vterm_color_rgb(&fg, 235, 241, 252); vterm_color_rgb(&bg, 8, 16, 28);
    vterm_screen_set_default_colors(m_screen, &fg, &bg); update(); emit scrollbackChanged();
}
void TerminalView::feed(const QByteArray &data) {
    if (!data.isEmpty()) clearSelection();
    m_cursorOn = true;
    vterm_input_write(m_vt, data.constData(), data.size()); vterm_screen_flush_damage(m_screen); update(); emit scrollbackChanged();
}
void TerminalView::geometryChange(const QRectF &n, const QRectF &o) { QQuickPaintedItem::geometryChange(n, o); sizeGrid(); }
void TerminalView::sizeGrid() {
    const int columns = qBound(2, int(width() / m_cellWidth), 240), rows = qBound(2, int(height() / m_cellHeight), 100);
    if (columns == m_columns && rows == m_rows) return;
    clearSelection();
    m_columns = columns; m_rows = rows; vterm_set_size(m_vt, rows, columns); update(); emit scrollbackChanged(); emit dimensionsChanged(); emit gridChanged(columns, rows);
}
void TerminalView::scrollTo(int offset) {
    const int next = qBound(0, offset, scrollbackLines());
    if (next == m_scrollOffset) return;
    clearSelection();
    m_scrollOffset = next; update(); emit scrollbackChanged();
}
bool TerminalView::viewportCell(int row, int col, VTermScreenCell *cell) const {
    const int index = scrollbackLines() - m_scrollOffset + row;
    if (index < scrollbackLines()) {
        const auto &line = m_scrollback[size_t(index)];
        if (col >= int(line.size())) return false;
        *cell = line[size_t(col)]; return true;
    }
    return vterm_screen_get_cell(m_screen, {index - scrollbackLines(), col}, cell);
}
void TerminalView::paint(QPainter *p) {
    p->fillRect(boundingRect(), QColor(8,16,28,235));
    for (int row = 0; row < m_rows; ++row) for (int col = 0; col < m_columns; ++col) {
        VTermScreenCell cell{}; if (!viewportCell(row, col, &cell) || cell.width == 0) continue;
        auto fg = cell.fg, bg = cell.bg;
        vterm_screen_convert_color_to_rgb(m_screen, &fg); vterm_screen_convert_color_to_rgb(m_screen, &bg);
        QColor f(fg.rgb.red, fg.rgb.green, fg.rgb.blue), b(bg.rgb.red, bg.rgb.green, bg.rgb.blue);
        if (cell.attrs.reverse) std::swap(f,b);
        if (!VTERM_COLOR_IS_DEFAULT_BG(&cell.bg) || cell.attrs.reverse)
            p->fillRect(QRectF(col*m_cellWidth,row*m_cellHeight,cell.width*m_cellWidth,m_cellHeight),b);
        const int index = row*m_columns+col;
        if (hasSelection() && index <= std::max(m_selectionStart,m_selectionEnd) && index+cell.width-1 >= std::min(m_selectionStart,m_selectionEnd))
            p->fillRect(QRectF(col*m_cellWidth,row*m_cellHeight,cell.width*m_cellWidth,m_cellHeight),QColor(63,120,196,180));
        if (cell.chars[0] && cell.chars[0] != UINT32_MAX && !cell.attrs.conceal) {
            QString text; for (uint32_t cp : cell.chars) { if (!cp || cp == UINT32_MAX) break; const char32_t c = cp; text += QString::fromUcs4(&c,1); }
            auto font = m_font; font.setBold(cell.attrs.bold); font.setItalic(cell.attrs.italic); font.setUnderline(cell.attrs.underline);
            p->setFont(font); p->setPen(f); p->drawText(QPointF(col*m_cellWidth,row*m_cellHeight+m_ascent),text);
        }
    }
    if (!m_scrollOffset && m_inputEnabled && m_cursorVisible && (!terminalFocused() || m_cursorOn)) {
        VTermPos pos; vterm_state_get_cursorpos(vterm_obtain_state(m_vt), &pos);
        const QRectF cursor(pos.col*m_cellWidth,pos.row*m_cellHeight,m_cellWidth,m_cellHeight);
        p->setPen(QColor("#8DBEFF"));
        if (terminalFocused()) p->fillRect(cursor.adjusted(0,0,0,-1), QColor(141,190,255,150));
        else p->drawRect(cursor.adjusted(0.5,0.5,-0.5,-0.5));
    }
}
QString TerminalView::screenText() const {
    return textRange(0,m_rows*m_columns-1);
}
QString TerminalView::selectedText() const {
    return hasSelection() ? textRange(std::min(m_selectionStart,m_selectionEnd),std::max(m_selectionStart,m_selectionEnd)) : QString();
}
QString TerminalView::textRange(int first, int last) const {
        QStringList lines;
        for (int row = first/m_columns; row <= last/m_columns; ++row) {
            QString line;
            const int start = row == first/m_columns ? first%m_columns : 0;
            const int end = row == last/m_columns ? last%m_columns : m_columns-1;
            for (int col = 0; col <= end; ++col) {
                VTermScreenCell cell{};
                if (!viewportCell(row,col,&cell)) { if(col>=start)line += ' '; continue; }
                if (col+std::max(1,int(cell.width)) <= start) continue;
                if (cell.width == 0 || cell.chars[0] == UINT32_MAX) continue;
                if (!cell.chars[0] || cell.attrs.conceal) { line += QString(std::max(1,int(cell.width)),' '); continue; }
                for (uint32_t cp : cell.chars) {
                    if (!cp || cp == UINT32_MAX) break;
                    const char32_t c = cp; line += QString::fromUcs4(&c,1);
                }
            }
            if(end==m_columns-1)while (line.endsWith(' ')) line.chop(1);
            lines += line;
        }
        return lines.join('\n');
}
QString TerminalView::clipboardText() const { return QGuiApplication::clipboard()->text(); }
void TerminalView::copyScreen() { QGuiApplication::clipboard()->setText(hasSelection() ? selectedText() : screenText()); }
void TerminalView::clearSelection() {
    const bool selected = hasSelection();
    m_selectionStart = m_selectionEnd = m_selectionAnchor = -1;
    if (selected) { update(); emit selectionChanged(); }
}
void TerminalView::paste(const QString &text) {
    if (!m_inputEnabled || text.toUtf8().size() > 16000 || text.contains(QChar::Null)) return;
    vterm_keyboard_start_paste(m_vt); textInput(text); vterm_keyboard_end_paste(m_vt);
}
void TerminalView::insertHistory(const QString &text) {
    if (!m_inputEnabled || text.toUtf8().size() > 16000) return;
    for (QChar c : text) if (c.unicode() < 32 || c.unicode() == 127 || c == QChar(0x2028) || c == QChar(0x2029)) return;
    textInput(text); forceActiveFocus();
}
void TerminalView::control(int code) { if (m_inputEnabled && code >= 1 && code <= 26) { clearSelection(); scrollTo(0); emit bytes(QByteArray(1,char(code))); } }
void TerminalView::textInput(const QString &text) { if(text.isEmpty())return; clearSelection(); scrollTo(0); m_cursorOn = true; update(); for (auto cp : text.toUcs4()) vterm_keyboard_unichar(m_vt, cp, VTERM_MOD_NONE); }
void TerminalView::keyPressEvent(QKeyEvent *e) {
    const auto mods = e->modifiers();
    if (e->key() == Qt::Key_Shift || e->key() == Qt::Key_Control || e->key() == Qt::Key_Alt || e->key() == Qt::Key_Meta) { e->accept(); return; }
    if (!m_alternateScreen && mods == Qt::ShiftModifier && (e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown || e->key() == Qt::Key_End)) {
        scrollTo(e->key() == Qt::Key_End ? 0 : m_scrollOffset + (e->key() == Qt::Key_PageUp ? 1 : -1) * std::max(1,m_rows-1));
        e->accept(); return;
    }
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) && e->key() == Qt::Key_C) { copyScreen(); return; }
    if (!m_inputEnabled) { e->ignore(); return; }
    m_cursorOn = true; update();
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) && e->key() == Qt::Key_V) { emit pasteRequested(); return; }
    clearSelection();
    scrollTo(0);
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
int TerminalView::cellIndex(const QPointF &position) const {
    return qBound(0,int(position.y()/m_cellHeight),m_rows-1)*m_columns + qBound(0,int(position.x()/m_cellWidth),m_columns-1);
}
void TerminalView::extendSelection(const QPointF &position) {
    if (m_selectionAnchor < 0) return;
    const int end = cellIndex(position);
    if (end == m_selectionAnchor && !hasSelection()) return;
    m_selectionStart = m_selectionAnchor; m_selectionEnd = end;
    update(); emit selectionChanged();
}
void TerminalView::mousePressEvent(QMouseEvent *e) {
    forceActiveFocus(); clearSelection(); m_selectionAnchor = cellIndex(e->position());
    if(m_inputEnabled)QGuiApplication::inputMethod()->show();update();e->accept();
}
void TerminalView::mouseMoveEvent(QMouseEvent *e) { if(e->buttons() & Qt::LeftButton)extendSelection(e->position()); e->accept(); }
void TerminalView::mouseReleaseEvent(QMouseEvent *e) { extendSelection(e->position()); m_selectionAnchor = -1; e->accept(); }
void TerminalView::wheelEvent(QWheelEvent *e) {
    if (m_alternateScreen) { e->ignore(); return; }
    m_wheelRemainder += !e->pixelDelta().isNull() ? e->pixelDelta().y()/m_cellHeight : e->angleDelta().y()/120.0*3;
    const int lines = int(m_wheelRemainder); m_wheelRemainder -= lines;
    scrollTo(m_scrollOffset + lines); e->accept();
}
void TerminalView::inputMethodEvent(QInputMethodEvent *e) { if(m_inputEnabled)textInput(e->commitString()); e->accept(); }
QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery q) const {
    if(q==Qt::ImEnabled)return m_inputEnabled;
    if(q==Qt::ImHints)return int(Qt::ImhNoPredictiveText|Qt::ImhNoAutoUppercase|Qt::ImhSensitiveData);
    if(q==Qt::ImCursorRectangle){ VTermPos p;vterm_state_get_cursorpos(vterm_obtain_state(m_vt),&p);return QRectF(p.col*m_cellWidth,p.row*m_cellHeight,m_cellWidth,m_cellHeight); }
    return QQuickPaintedItem::inputMethodQuery(q);
}
