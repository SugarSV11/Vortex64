#pragma once

#include <QColor>
#include <QFont>
#include <QString>

class QApplication;
class QWidget;

// The one place where the look of Vortex64 is defined: the colour tokens of the dark theme, the metrics
// (radius, spacing, heights, widths) and the style sheet built from them. Nothing else in the frontend
// carries a colour or a size of its own: widgets ask for a role (setProperty("vx", "card"), an object name
// listed here) or read a token.
namespace Vortex
{
struct Tokens
{
    // surfaces
    QColor Background{0x16, 0x17, 0x1a};
    QColor Sidebar{0x1a, 0x1c, 0x20};
    QColor Surface{0x1f, 0x22, 0x27};
    QColor SurfaceElevated{0x27, 0x2b, 0x31};
    QColor Hover{0x2d, 0x32, 0x39};
    QColor Selected{0x3a, 0x42, 0x50};
    QColor Border{0x2f, 0x34, 0x3b};
    // text
    QColor TextPrimary{0xe8, 0xea, 0xed};
    QColor TextSecondary{0x9a, 0xa0, 0xa9};
    QColor TextDisabled{0x5d, 0x63, 0x6c};
    // accents
    QColor Accent{0x7f, 0x9c, 0xc8};
    QColor Danger{0xe0, 0x5d, 0x5d};
    QColor Warning{0xe0, 0xb0, 0x4a};
    QColor Success{0x5f, 0xb9, 0x7a};
    QColor Favorite{0xf2, 0xb8, 0x2b};

    // metrics (logical pixels; Qt scales them with the screen)
    int CornerRadius = 12;
    int SmallRadius = 8;
    int Spacing = 12;
    int SmallSpacing = 6;
    int ControlHeight = 32;
    int SidebarWidth = 200;
    int HeaderHeight = 56;
    int DetailPanelHeight = 112;
    int CardWidth = 150;
    int CardCoverHeight = 130;

    QString MonoFamily = QStringLiteral("JetBrains Mono, DejaVu Sans Mono, Liberation Mono, monospace");
};

const Tokens & T();

// Fusion style + palette + the style sheet; call once, before the first window.
void ApplyTheme(QApplication & App);
// The style sheet (also used by tests that create widgets without ApplyTheme)
QString StyleSheet();
QFont MonoFont(int PointSize = 0);

// A widget gets the look of a role (see the selectors in StyleSheet()):
//   "page" (a page background), "panel", "card", "toolbar", "title", "subtitle", "muted", "mono", "accent"
void SetRole(QWidget * Widget, const char * Role);
} // namespace Vortex
