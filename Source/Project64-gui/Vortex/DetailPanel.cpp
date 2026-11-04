#include "DetailPanel.h"
#include "Covers.h"
#include "Icons.h"
#include "Store.h"
#include "Strings.h"
#include "Theme.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <algorithm>

namespace Vortex
{
namespace
{
// A title that never asks for more width than it has: it is elided at the end (the full text is the tooltip)
class ElidedLabel : public QLabel
{
public:
    using QLabel::QLabel;
    void SetFullText(const QString & Text)
    {
        m_Full = Text;
        setToolTip(Text);
        Elide();
    }

protected:
    void resizeEvent(QResizeEvent * Event) override
    {
        QLabel::resizeEvent(Event);
        Elide();
    }

private:
    void Elide()
    {
        setText(fontMetrics().elidedText(m_Full, Qt::ElideRight, std::max(0, width())));
    }
    QString m_Full;
};
} // namespace

DetailPanel::DetailPanel(LibraryModel * Model, QWidget * Parent) :
    QWidget(Parent),
    m_Model(Model)
{
    setObjectName("VxDetail");
    SetRole(this, "detail");
    setAttribute(Qt::WA_StyledBackground, true);
    // the height is what the content needs (never less than the design's): a fixed one cut the last line with a taller font
    setMinimumHeight(T().DetailPanelHeight);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    QHBoxLayout * Layout = new QHBoxLayout(this);
    Layout->setContentsMargins(18, 14, 18, 14);
    Layout->setSpacing(16);
    m_Thumb = new QLabel(this);
    m_Thumb->setFixedSize(84, 84);
    m_Thumb->setObjectName("VxDetailThumb");

    QVBoxLayout * Text = new QVBoxLayout;
    Text->setSpacing(6);
    m_Name = new ElidedLabel(this);
    m_Name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_Name->setObjectName("VxDetailName");
    QFont Font = m_Name->font();
    Font.setPixelSize(20);
    Font.setWeight(QFont::Bold);
    m_Name->setFont(Font);
    QGridLayout * Grid = new QGridLayout;
    Grid->setHorizontalSpacing(40);
    Grid->setVerticalSpacing(3);
    m_Region = new QLabel(this);
    m_Size = new QLabel(this);
    m_Format = new QLabel(this);
    m_Last = new QLabel(this);
    m_Played = new QLabel(this);
    for (QLabel * L : {m_Region, m_Size, m_Format, m_Last, m_Played})
    {
        L->setTextFormat(Qt::RichText);
    }
    m_Region->setObjectName("VxDetailRegion");
    m_Size->setObjectName("VxDetailSize");
    m_Format->setObjectName("VxDetailFormat");
    m_Last->setObjectName("VxDetailLast");
    m_Played->setObjectName("VxDetailPlayed");
    Grid->addWidget(m_Region, 0, 0);
    Grid->addWidget(m_Size, 1, 0);
    Grid->addWidget(m_Format, 2, 0);
    Grid->addWidget(m_Last, 0, 1);
    Grid->addWidget(m_Played, 1, 1);
    Grid->setColumnStretch(2, 1);
    Text->addWidget(m_Name);
    Text->addLayout(Grid);

    m_Start = new QPushButton(Tr("Iniciar", "Start"), this);
    m_Start->setObjectName("VxStart");
    m_Start->setProperty("vx", "primary");
    m_Start->setIcon(MakeIcon(Icon::Play, 16));
    m_Start->setIconSize(QSize(16, 16));
    m_Start->setMinimumWidth(176);
    m_Start->setMinimumHeight(46);
    m_Start->setDefault(false);
    m_Start->setCursor(Qt::PointingHandCursor);

    Layout->addWidget(m_Thumb);
    Layout->addLayout(Text, 1);
    Layout->addWidget(m_Start, 0, Qt::AlignVCenter);
    connect(m_Start, &QPushButton::clicked, this, [this]() {
        if (!m_Path.isEmpty())
        {
            Q_EMIT StartRequested(m_Path);
        }
    });
    connect(&Covers::Instance(), &Covers::CoverReady, this, [this](const QString & Rom) {
        if (Rom == m_Path)
        {
            Refresh();
        }
    });
    connect(&Store::Instance(), &Store::HistoryChanged, this, &DetailPanel::Refresh);
    SetRom(QString());
}

void DetailPanel::SetRom(const QString & Path)
{
    m_Path = Path;
    Refresh();
}

// "label: value" with the label quiet and the value clear
static QString Field(const QString & Label, const QString & Value)
{
    const Tokens & K = T();
    return QStringLiteral("<span style='color:%1'>%2</span> <span style='color:%3'>%4</span>").arg(K.TextSecondary.name(), Label.toHtmlEscaped(), K.TextPrimary.name(), Value.toHtmlEscaped());
}

void DetailPanel::Refresh()
{
    const Entry * E = m_Path.isEmpty() ? nullptr : m_Model->Find(m_Path);
    m_Start->setEnabled(E != nullptr);
    if (E == nullptr)
    {
        m_Thumb->setPixmap(Covers::Placeholder(QStringLiteral("64"), m_Thumb->size(), devicePixelRatioF()));
        static_cast<ElidedLabel *>(m_Name)->SetFullText(Tr("Selecciona un juego", "Select a game"));
        m_Region->setText(Field(Tr("Región:", "Region:"), "—"));
        m_Size->setText(Field(Tr("Tamaño:", "Size:"), "—"));
        m_Format->setText(Field(Tr("Formato:", "Format:"), "—"));
        m_Last->setText(Field(Tr("Jugado por última vez:", "Last played:"), "—"));
        m_Played->setText(Field(Tr("Tiempo de juego:", "Play time:"), "—"));
        return;
    }
    m_Thumb->setPixmap(Covers::Instance().Cover(*E, m_Thumb->size(), devicePixelRatioF()));
    static_cast<ElidedLabel *>(m_Name)->SetFullText(LibraryModel::DisplayName(*E));
    m_Region->setText(Field(Tr("Región:", "Region:"), RegionName(E->Country) == QStringLiteral("—") ? Tr("desconocida", "unknown") : RegionLong(E->Country) + QStringLiteral(" (") + RegionName(E->Country) + QLatin1Char(')')));
    m_Size->setText(Field(Tr("Tamaño:", "Size:"), LibraryModel::SizeText(E->Size)));
    m_Format->setText(Field(Tr("Formato:", "Format:"), E->Format));
    const PlayRecord Record = Store::Instance().Record(E->Path);
    m_Last->setText(Field(Tr("Jugado por última vez:", "Last played:"), LibraryModel::DateText(Record.LastPlayed)));
    m_Played->setText(Field(Tr("Tiempo de juego:", "Play time:"), LibraryModel::PlayTimeText(Record.Seconds)));
}
} // namespace Vortex
