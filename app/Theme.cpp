#include "Theme.h"

#include <QApplication>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>

namespace Theme {
namespace {

Mode g_mode = Mode::Dark;
Tokens g_tokens;

Tokens makeTokens(Mode m)
{
    Tokens t;
    if (m == Mode::Dark) {
        t.page = QColor("#0d0d0d");
        t.surface = QColor("#1a1a19");
        t.raised = QColor("#242422");
        t.hairline = QColor("#2f2f2c");
        t.ink = QColor("#ffffff");
        t.ink2 = QColor("#c3c2b7");
        t.muted = QColor("#898781");
        t.accent = QColor("#3987e5");
    } else {
        t.page = QColor("#f1f1ee");
        t.surface = QColor("#fcfcfb");
        t.raised = QColor("#ffffff");
        t.hairline = QColor("#e1e0d9");
        t.ink = QColor("#0b0b0b");
        t.ink2 = QColor("#52514e");
        t.muted = QColor("#898781");
        t.accent = QColor("#2a78d6");
    }
    t.onAccent = QColor("#ffffff");
    t.good = QColor("#0ca30c");
    t.warning = QColor("#fab219");
    t.serious = QColor("#ec835a");
    t.critical = QColor("#d03b3b");
    return t;
}

QString rgba(const QColor &c, double alpha)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

QString styleSheet(const Tokens &t)
{
    QString s = QStringLiteral(R"(
* { outline: none; }
QWidget { color: @ink; font-size: 13px; }
QMainWindow, #Page { background: @page; }
QToolTip { background: @raised; color: @ink; border: 1px solid @hairline; padding: 6px 8px; border-radius: 6px; }

#Card { background: @surface; border: 1px solid @hairline; border-radius: 12px; }
#Header { background: @surface; border-bottom: 1px solid @hairline; }
#Sidebar { background: @surface; border-right: 1px solid @hairline; }
#AppTitle { font-size: 16px; font-weight: 600; }
#AppSubtitle, #Muted { color: @muted; }
#SectionTitle { color: @muted; font-size: 11px; font-weight: 600; letter-spacing: 1px; padding-top: 10px; }
#Hint { color: @muted; font-size: 12px; }
#KeyLabel { color: @muted; }
#ValueLabel { color: @ink; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: @raised; border: 1px solid @hairline; border-radius: 7px;
    padding: 5px 8px; min-height: 22px; selection-background-color: @accent; selection-color: @onAccent;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border: 1px solid @accent; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: @muted; background: @surface; }
QSpinBox::up-button, QDoubleSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::down-button { width: 0px; border: none; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: @raised; border: 1px solid @hairline; selection-background-color: @accentSoft; selection-color: @ink; padding: 4px; outline: none; }

QPushButton { background: @raised; border: 1px solid @hairline; border-radius: 8px; padding: 7px 14px; }
QPushButton:hover { border-color: @muted; }
QPushButton:disabled { color: @muted; }
QPushButton#Primary { background: @accent; color: @onAccent; border: none; font-weight: 600; padding: 8px 18px; }
QPushButton#Primary:hover { background: @accentHover; }
QPushButton#Danger { background: @critical; color: #ffffff; border: none; font-weight: 600; padding: 8px 18px; }
QPushButton#Ghost, QToolButton#Ghost { background: transparent; border: none; border-radius: 8px; padding: 6px; }
QPushButton#Ghost:hover, QToolButton#Ghost:hover { background: @raised; }

QToolButton#Segment { background: transparent; border: none; border-radius: 7px; padding: 7px 14px; color: @ink2; font-weight: 600; }
QToolButton#Segment:checked { background: @raised; color: @ink; border: 1px solid @hairline; }
#SegmentBar { background: @page; border-radius: 9px; }

QTabWidget::pane { border: none; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab { background: transparent; color: @muted; padding: 8px 14px; border: none; border-bottom: 2px solid transparent; font-weight: 600; }
QTabBar::tab:selected { color: @ink; border-bottom: 2px solid @accent; }
QTabBar::tab:hover:!selected { color: @ink2; }

QTableWidget, QTableView { background: @surface; border: none; gridline-color: transparent; alternate-background-color: @altRow; selection-background-color: @accentSoft; }
QHeaderView::section { background: @surface; color: @muted; border: none; border-bottom: 1px solid @hairline; padding: 6px 8px; font-weight: 600; font-size: 12px; }
QPlainTextEdit { background: @surface; border: none; selection-background-color: @accentSoft; }

QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @hairline; border-radius: 3px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: @muted; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: @hairline; border-radius: 3px; min-width: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QSplitter::handle { background: transparent; }
QStatusBar { background: @surface; border-top: 1px solid @hairline; color: @muted; }
QStatusBar QLabel { color: @muted; }
QCheckBox { spacing: 8px; }
QMenu { background: @raised; border: 1px solid @hairline; padding: 4px; }
QMenu::item { padding: 6px 18px; border-radius: 5px; }
QMenu::item:selected { background: @accentSoft; }
)");
    s.replace(QLatin1String("@page"), t.page.name());
    s.replace(QLatin1String("@surface"), t.surface.name());
    s.replace(QLatin1String("@raised"), t.raised.name());
    s.replace(QLatin1String("@hairline"), t.hairline.name());
    s.replace(QLatin1String("@ink2"), t.ink2.name());
    s.replace(QLatin1String("@ink"), t.ink.name());
    s.replace(QLatin1String("@muted"), t.muted.name());
    s.replace(QLatin1String("@accentSoft"), rgba(t.accent, 0.28));
    s.replace(QLatin1String("@accentHover"), t.accent.lighter(112).name());
    s.replace(QLatin1String("@accent"), t.accent.name());
    s.replace(QLatin1String("@onAccent"), t.onAccent.name());
    s.replace(QLatin1String("@critical"), t.critical.name());
    s.replace(QLatin1String("@altRow"), rgba(t.ink, 0.025));
    return s;
}

QPalette makePalette(const Tokens &t)
{
    QPalette p;
    p.setColor(QPalette::Window, t.page);
    p.setColor(QPalette::WindowText, t.ink);
    p.setColor(QPalette::Base, t.raised);
    p.setColor(QPalette::AlternateBase, t.surface);
    p.setColor(QPalette::Text, t.ink);
    p.setColor(QPalette::Button, t.raised);
    p.setColor(QPalette::ButtonText, t.ink);
    p.setColor(QPalette::Highlight, t.accent);
    p.setColor(QPalette::HighlightedText, t.onAccent);
    p.setColor(QPalette::ToolTipBase, t.raised);
    p.setColor(QPalette::ToolTipText, t.ink);
    p.setColor(QPalette::PlaceholderText, t.muted);
    p.setColor(QPalette::Mid, t.hairline);
    p.setColor(QPalette::Dark, t.hairline);
    p.setColor(QPalette::Light, t.raised);
    p.setColor(QPalette::Disabled, QPalette::Text, t.muted);
    p.setColor(QPalette::Disabled, QPalette::WindowText, t.muted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, t.muted);
    return p;
}

} // namespace

void apply(QApplication &app, Mode m)
{
    g_mode = m;
    g_tokens = makeTokens(m);
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setPalette(makePalette(g_tokens));
    app.setStyleSheet(styleSheet(g_tokens));
}

Mode mode()
{
    return g_mode;
}

const Tokens &tokens()
{
    if (!g_tokens.page.isValid())
        g_tokens = makeTokens(g_mode);
    return g_tokens;
}

QIcon icon(const QString &name, const QColor &color, int size)
{
    const qreal dpr = 2.0;
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = size;
    QPen pen(color, s / 10.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (name == QLatin1String("play")) {
        QPainterPath path;
        path.moveTo(s * 0.30, s * 0.20);
        path.lineTo(s * 0.80, s * 0.50);
        path.lineTo(s * 0.30, s * 0.80);
        path.closeSubpath();
        p.setBrush(color);
        p.drawPath(path);
    } else if (name == QLatin1String("stop")) {
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(s * 0.25, s * 0.25, s * 0.5, s * 0.5), s * 0.08, s * 0.08);
    } else if (name == QLatin1String("refresh") || name == QLatin1String("sync")) {
        p.drawArc(QRectF(s * 0.2, s * 0.2, s * 0.6, s * 0.6), 60 * 16, 280 * 16);
        QPainterPath head;
        head.moveTo(s * 0.62, s * 0.12);
        head.lineTo(s * 0.68, s * 0.28);
        head.lineTo(s * 0.52, s * 0.32);
        p.drawPath(head);
    } else if (name == QLatin1String("sun")) {
        p.drawEllipse(QPointF(s / 2, s / 2), s * 0.16, s * 0.16);
        for (int i = 0; i < 8; ++i) {
            p.save();
            p.translate(s / 2, s / 2);
            p.rotate(i * 45);
            p.drawLine(QPointF(0, s * 0.28), QPointF(0, s * 0.38));
            p.restore();
        }
    } else if (name == QLatin1String("moon")) {
        QPainterPath outer;
        outer.addEllipse(QPointF(s * 0.5, s * 0.5), s * 0.3, s * 0.3);
        QPainterPath cut;
        cut.addEllipse(QPointF(s * 0.66, s * 0.38), s * 0.24, s * 0.24);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawPath(outer.subtracted(cut));
    } else if (name == QLatin1String("folder")) {
        QPainterPath path;
        path.moveTo(s * 0.15, s * 0.28);
        path.lineTo(s * 0.40, s * 0.28);
        path.lineTo(s * 0.48, s * 0.36);
        path.lineTo(s * 0.85, s * 0.36);
        path.lineTo(s * 0.85, s * 0.76);
        path.lineTo(s * 0.15, s * 0.76);
        path.closeSubpath();
        p.drawPath(path);
    } else if (name == QLatin1String("volume") || name == QLatin1String("mute")) {
        // Speaker, with sound waves or a cross
        QPainterPath spk;
        spk.moveTo(s * 0.14, s * 0.40);
        spk.lineTo(s * 0.28, s * 0.40);
        spk.lineTo(s * 0.46, s * 0.24);
        spk.lineTo(s * 0.46, s * 0.76);
        spk.lineTo(s * 0.28, s * 0.60);
        spk.lineTo(s * 0.14, s * 0.60);
        spk.closeSubpath();
        p.setBrush(color);
        p.drawPath(spk);
        p.setBrush(Qt::NoBrush);
        if (name == QLatin1String("volume")) {
            p.drawArc(QRectF(s * 0.40, s * 0.34, s * 0.24, s * 0.32), -50 * 16, 100 * 16);
            p.drawArc(QRectF(s * 0.38, s * 0.22, s * 0.44, s * 0.56), -50 * 16, 100 * 16);
        } else {
            p.drawLine(QPointF(s * 0.60, s * 0.38), QPointF(s * 0.84, s * 0.62));
            p.drawLine(QPointF(s * 0.84, s * 0.38), QPointF(s * 0.60, s * 0.62));
        }
    } else if (name == QLatin1String("logo")) {
        // Satellite dish
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(0, 0, s, s), s * 0.24, s * 0.24);
        QPen w(Qt::white, s / 12.0, Qt::SolidLine, Qt::RoundCap);
        p.setPen(w);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(s * 0.18, s * 0.18, s * 0.64, s * 0.64), 200 * 16, 140 * 16);
        p.drawLine(QPointF(s * 0.5, s * 0.5), QPointF(s * 0.70, s * 0.30));
        p.drawLine(QPointF(s * 0.5, s * 0.5), QPointF(s * 0.40, s * 0.78));
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(s * 0.72, s * 0.28), s * 0.05, s * 0.05);
    }
    return QIcon(pm);
}

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text.toUpper(), parent);
    l->setObjectName(QStringLiteral("SectionTitle"));
    return l;
}

} // namespace Theme
