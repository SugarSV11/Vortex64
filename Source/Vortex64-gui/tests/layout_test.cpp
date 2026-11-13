// The layout of the windows of Vortex64 with real geometry, in both languages, at several window sizes (and, when the runner sets
// QT_SCALE_FACTOR, at several screen scales): the sidebar of the settings window (every row its own rectangle; selected and hover
// painted inside it), the strip under the library (every line whole), long titles. Offscreen Qt, isolated XDG directories:
//
//   layout-test
#include "ui_test_env.h"
#include "../Vortex/DetailPanel.h"
#include "../Vortex/Language.h"
#include "../Vortex/Theme.h"

#include <QHoverEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

using namespace Vortex;

// The rows of the list: their rectangles are the rows' own (as tall as asked, in order, none over another)
static void CheckRows(QListWidget * Nav, const char * What)
{
    QRect Previous;
    for (int i = 0; i < Nav->count(); i++)
    {
        const QRect Rect = Nav->visualRect(Nav->model()->index(i, 0));
        CHECK(Rect.height() == Nav->item(i)->sizeHint().height());
        CHECK(Rect.height() >= Nav->fontMetrics().height() + 8);
        CHECK(Nav->viewport()->rect().contains(Rect));
        if (i > 0)
        {
            CHECK(Rect.top() > Previous.bottom()); // not one pixel over the row above
        }
        Previous = Rect;
    }
    printf("%s: %d rows of %d px\n", What, Nav->count(), Nav->count() > 0 ? Nav->visualRect(Nav->model()->index(0, 0)).height() : 0);
}

// The colour at a point of the viewport as painted (the view is rendered to an image)
static QColor PixelAt(const QImage & Image, const QPoint & Logical)
{
    const qreal Ratio = Image.devicePixelRatio();
    return Image.pixelColor(int(Logical.x() * Ratio), int(Logical.y() * Ratio));
}

// Paints the view and looks, for the rows, where the colour of a state is: only inside the row that has the state
static void CheckPainted(QListWidget * Nav, const QColor & State, int Row, const char * What)
{
    QApplication::processEvents();
    const QImage Image = Nav->viewport()->grab().toImage();
    const QRect Own = Nav->visualRect(Nav->model()->index(Row, 0));
    const int X = Own.left() + 4; // (inside the row, before its icon)
    bool InsideOwn = false;
    int Outside = 0;
    for (int Y = 0; Y < Nav->viewport()->height(); Y++)
    {
        if (PixelAt(Image, QPoint(X, Y)) == State)
        {
            if (Y >= Own.top() && Y <= Own.bottom())
            {
                InsideOwn = true;
            }
            else
            {
                Outside++;
            }
        }
    }
    printf("%s row %d: painted inside=%d, outside=%d (wanted %s, found %s at the middle of the row, image %dx%d ratio %.2f)\n", What, Row, InsideOwn ? 1 : 0, Outside, State.name(QColor::HexArgb).toUtf8().constData(),
           PixelAt(Image, QPoint(X, Own.center().y())).name(QColor::HexArgb).toUtf8().constData(), Image.width(), Image.height(), Image.devicePixelRatio());
    CHECK(InsideOwn);
    CHECK(Outside == 0);
}

// Every label of the strip is whole: as tall as it needs, and inside the panel; the panel inside the window
static void CheckStrip(MainWindow & Window, const char * What)
{
    Vortex::Shell * Shell = Window.LibraryShell();
    DetailPanel * Panel = Shell->Page("library")->Detail();
    QApplication::processEvents();
    CHECK(Panel->isVisible());
    CHECK(Panel->height() >= Panel->layout()->minimumSize().height());
    CHECK(Panel->height() >= T().DetailPanelHeight);
    const QPoint Origin = Panel->mapTo(&Window, QPoint(0, 0));
    CHECK(Origin.y() + Panel->height() <= Window.height()); // the strip ends where the window ends: nothing under the border
    for (QLabel * Label : Panel->findChildren<QLabel *>())
    {
        if (!Label->isVisible() || Label->text().isEmpty())
        {
            continue;
        }
        const QRect In = QRect(Label->mapTo(Panel, QPoint(0, 0)), Label->size());
        CHECK(Panel->rect().contains(In));
        CHECK(Label->height() >= Label->sizeHint().height());
        CHECK(Label->height() >= Label->fontMetrics().height());
        if (!Panel->rect().contains(In) || Label->height() < Label->sizeHint().height())
        {
            printf("%s: label %s %d,%d %dx%d in a panel of %dx%d (needs %d)\n", What, Label->objectName().toUtf8().constData(), In.x(), In.y(), In.width(), In.height(), Panel->width(), Panel->height(), Label->sizeHint().height());
        }
    }
    // the start button is centred on the strip, and the cover too
    QPushButton * Start = Panel->StartButton();
    const int StartMiddle = Start->mapTo(Panel, QPoint(0, 0)).y() + Start->height() / 2;
    CHECK(qAbs(StartMiddle - Panel->height() / 2) <= 2);
    QLabel * Thumb = Panel->findChild<QLabel *>("VxDetailThumb");
    CHECK(Thumb != nullptr);
    if (Thumb != nullptr)
    {
        const int ThumbMiddle = Thumb->mapTo(Panel, QPoint(0, 0)).y() + Thumb->height() / 2;
        CHECK(qAbs(ThumbMiddle - Panel->height() / 2) <= 2);
    }
}

int main(int argc, char ** argv)
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    QApplication Qt(argc, argv);
    ApplyTheme(Qt); // (the look of the application: what the rows are painted with)
    printf("screen scale: %s, device pixel ratio %.2f\n", qgetenv("QT_SCALE_FACTOR").isEmpty() ? "1" : qgetenv("QT_SCALE_FACTOR").constData(), Qt.devicePixelRatio());
    UiTestApp App;
    const QString LongTitle = "The Legend of Zelda - Ocarina of Time - Master Quest - Extended Edition With A Very Long Title Indeed (USA)";
    CHECK(App.Start(argv[0], LongTitle));
    if (App.Window == nullptr || App.Window->LibraryShell() == nullptr)
    {
        printf("FAILED\n");
        return 2;
    }
    MainWindow & Window = *App.Window;
    const int MinimumWidth = Window.minimumSizeHint().width();

    for (const QString & Code : {QString("es"), QString("en")})
    {
        ApplyLanguage(Code);
        QApplication::processEvents();
        for (const QSize & Size : {QSize(1280, 800), QSize(1024, 640), QSize(900, 600), QSize(1920, 1080)})
        {
            Window.resize(Size);
            QApplication::processEvents();
            const QString Context = Code + " " + QString::number(Size.width()) + "x" + QString::number(Size.height());
            Vortex::Shell * Shell = Window.LibraryShell();

            // ---- the strip under the library: with a game, and without one ("Selecciona un juego")
            Shell->ShowPage("library");
            Shell->SelectRom(App.Roms + "/Super Mario 64 (USA).z64");
            CheckStrip(Window, (Context + " game").toUtf8().constData());
            Shell->Page("library")->Detail()->SetRom(QString());
            CheckStrip(Window, (Context + " no game").toUtf8().constData());
            CHECK(Shell->Page("library")->Detail()->findChild<QLabel *>("VxDetailName") != nullptr);
            // a long title does not push the window wider nor cut the strip
            Shell->SelectRom(App.Roms + "/" + LongTitle + ".z64");
            CheckStrip(Window, (Context + " long title").toUtf8().constData());
            CHECK(Window.minimumSizeHint().width() <= MinimumWidth);
            QLabel * Name = Shell->Page("library")->Detail()->findChild<QLabel *>("VxDetailName");
            CHECK(Name != nullptr && Name->width() <= Shell->Page("library")->Detail()->width());
            CHECK(Name != nullptr && Name->toolTip().contains("Very Long Title"));
            Shell->SelectRom(App.Roms + "/Super Mario 64 (USA).z64");
        }

        // ---- the sidebar of the settings window: every row on its own, in every state
        SettingsDialog Dialog(false, &Window);
        Dialog.show();
        QApplication::processEvents();
        QListWidget * Nav = Dialog.findChild<QListWidget *>("VxSettingsNav");
        CHECK(Nav != nullptr);
        if (Nav == nullptr)
        {
            continue;
        }
        CHECK(Nav->count() == Dialog.CategoryIds().size());
        CheckRows(Nav, (Code + " settings").toUtf8().constData());
        for (int Row = 0; Row < Nav->count(); Row++)
        {
            Nav->setCurrentRow(Row);
            CheckPainted(Nav, T().Selected, Row, (Code + " selected").toUtf8().constData());
        }
        // hover: the pointer over a row (not the selected one) paints that row only
        Nav->setCurrentRow(0);
        for (int Row = 1; Row < Nav->count(); Row++)
        {
            const QPoint Over = Nav->visualRect(Nav->model()->index(Row, 0)).center();
            QMouseEvent Move(QEvent::MouseMove, QPointF(Over), QPointF(Nav->viewport()->mapToGlobal(Over)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QHoverEvent Hover(QEvent::HoverMove, QPointF(Over), QPointF(Nav->viewport()->mapToGlobal(Over)), QPointF(0, 0));
            QApplication::sendEvent(Nav->viewport(), &Hover);
            QApplication::sendEvent(Nav->viewport(), &Move);
            CheckPainted(Nav, T().Hover, Row, (Code + " hover").toUtf8().constData());
            CheckPainted(Nav, T().Selected, 0, (Code + " selected while hovering another").toUtf8().constData());
        }
        // the colour of the hover is not left on the first row, nor the selection on another
        Dialog.close();
    }

    printf("%s\n", g_Failures == 0 ? "PASS" : "FAILED");
    Window.close();
    delete App.Debugger;
    return g_Failures == 0 ? 0 : 1;
}
