#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;
class QLabel;
class QWidget;

// Colour tokens and the application style sheet (dark and light).
namespace Theme {

enum class Mode { Dark, Light };

struct Tokens
{
    QColor page;      // window background
    QColor surface;   // cards
    QColor raised;    // inputs, hovered rows
    QColor hairline;  // borders, grid lines
    QColor ink;       // primary text
    QColor ink2;      // secondary text
    QColor muted;     // labels, axes
    QColor accent;    // brand / data series
    QColor onAccent;  // text on accent
    // Status colours: always shown together with a text label
    QColor good;
    QColor warning;
    QColor serious;
    QColor critical;
};

void apply(QApplication &app, Mode mode);
Mode mode();
const Tokens &tokens();

// Vector icons drawn in the given colour: play, stop, refresh, sun, moon, folder, sync, volume, mute, logo
QIcon icon(const QString &name, const QColor &color, int size = 18);

// Small upper-case section caption
QLabel *sectionTitle(const QString &text, QWidget *parent = nullptr);

} // namespace Theme
