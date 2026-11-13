#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QSize>

// The icons of Vortex64: monochrome line icons drawn with QPainter on a 24 x 24 grid (resolution
// independent, tinted with a theme colour), the flags of the regions and the tornado logo.
namespace Vortex
{
enum class Icon
{
    Library,
    Recent,
    Favorites,
    Disk,
    Settings,
    Debugger,
    About,
    Search,
    ViewGrid,
    ViewList,
    ViewCompact,
    Folder,
    Play,
    Pause,
    Stop,
    Star,
    StarOutline,
    Refresh,
    Plus,
    Minus,
    Close,
    Camera,
    Save,
    Load,
    Fullscreen,
    Gamepad,
    Chip,
    Audio,
    Graphics,
    Plugin,
    Advanced,
    Cheat,
    Info,
    Trash,
    Eject,
    Check,
    Keyboard,
};

QPixmap IconPixmap(Icon Which, const QColor & Color, int Size, qreal DevicePixelRatio = 1.0);
// An icon with the normal, active and disabled variants from the theme
QIcon MakeIcon(Icon Which, int Size = 20);
QIcon MakeIcon(Icon Which, const QColor & Color, int Size = 20);

// The tornado of the logo, drawn in Rect (a square) with Painter
void PaintLogo(QPainter & Painter, const QRectF & Rect, const QColor & Color);
QPixmap LogoPixmap(int Size, const QColor & Color, qreal DevicePixelRatio = 1.0);
QIcon AppIcon();

// The flag of a region of the ROM (country byte of the header: 'E' USA, 'J' Japan, 'P' Europe, ...)
void PaintFlag(QPainter & Painter, const QRectF & Rect, char Country);
QString RegionName(char Country); // "USA", "JPN", "EUR", ...
QString RegionLong(char Country); // "NTSC-U", "NTSC-J", "PAL"
} // namespace Vortex
