#include "stdafx.h"
#include <stdio.h>
#include "DebugCommon.h"

#include <QRegularExpression>
#include <QRegularExpressionValidator>

NumberEdit::NumberEdit(QWidget * Parent, Display Mode, int MaxDigits) :
    QLineEdit(Parent),
    m_Display(Mode),
    m_MaxDigits(MaxDigits)
{
    setMaxLength(MaxDigits);
    setValidator(new QRegularExpressionValidator(QRegularExpression(Mode == DisplayHex ? "[0-9A-Fa-f]*" : "[0-9]*"), this));
    connect(this, &QLineEdit::textEdited, this, [this](const QString & Text) {
        if (m_Display == DisplayHex && Text != Text.toUpper())
        {
            const int Cursor = cursorPosition();
            setText(Text.toUpper());
            setCursorPosition(Cursor);
        }
        Q_EMIT ValueEdited();
    });
}

uint32_t NumberEdit::Value() const
{
    return (uint32_t)text().toULongLong(nullptr, m_Display == DisplayHex ? 16 : 10);
}

void NumberEdit::SetValue(uint32_t Value, bool ZeroExtend)
{
    if (m_Display == DisplayHex)
    {
        setText(ZeroExtend ? QString("%1").arg(Value, m_MaxDigits, 16, QChar('0')).toUpper() : QString::number(Value, 16).toUpper());
    }
    else
    {
        setText(QString::number(Value));
    }
}

#include <QMessageBox>
#include <QMetaObject>

int MessageBox(void * /*Parent*/, const wchar_t * Text, const wchar_t * Caption, unsigned /*Type*/)
{
    const QString Message = QString::fromWCharArray(Text), Title = QString::fromWCharArray(Caption);
    QCoreApplication * App = QCoreApplication::instance();
    if (App == nullptr)
    {
        fprintf(stderr, "%s: %s\n", Title.toUtf8().constData(), Message.toUtf8().constData());
        return 0;
    }
    auto Show = [Message, Title]() { QMessageBox::warning(nullptr, Title, Message); };
    if (QThread::currentThread() == App->thread())
    {
        Show();
    }
    else
    {
        QMetaObject::invokeMethod(App, Show, Qt::BlockingQueuedConnection);
    }
    return 0;
}
