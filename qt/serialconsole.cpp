#include "serialconsole.h"
#include <QCheckBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QVBoxLayout>

SerialConsole::SerialConsole(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("Serial Console (dev unit)"));
    resize(640, 400);

    log = new QPlainTextEdit(this);
    log->setReadOnly(true);
    log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log->setMaximumBlockCount(5000);
    log->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    input = new QLineEdit(this);
    input->setPlaceholderText(tr("Type a line and press Enter to send it to the guest"));
    newline = new QCheckBox(tr("Add newline"), this);
    newline->setChecked(true);
    QPushButton *clearButton = new QPushButton(tr("Clear"), this);

    QHBoxLayout *row = new QHBoxLayout;
    row->addWidget(input, 1);
    row->addWidget(newline);
    row->addWidget(clearButton);
    QVBoxLayout *col = new QVBoxLayout(this);
    col->addWidget(log, 1);
    col->addLayout(row);

    connect(input, &QLineEdit::returnPressed, this, [this]() {
        QByteArray bytes = input->text().toLatin1();
        if (newline->isChecked()) bytes.append('\n');
        input->clear();
        emit lineEntered(bytes);
    });
    connect(clearButton, &QPushButton::clicked, this, &SerialConsole::clear);
}

void SerialConsole::appendOutput(const QByteArray &bytes)
{
    // One insert per call, not per byte. '\r' is dropped (a guest sending CR LF shouldn't double-space); anything else is
    // shown as Latin-1, so a stray binary byte can't take the terminal with it.
    QString text;
    text.reserve(bytes.size());
    for (char c : bytes) {
        if (c == '\r') continue;
        text.append(QChar(static_cast<uchar>(c)));
    }
    if (text.isEmpty()) return;
    QTextCursor cursor = log->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    log->setTextCursor(cursor);
    log->ensureCursorVisible();
}

void SerialConsole::clear()
{
    log->clear();
}
