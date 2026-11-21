#ifndef SERIALCONSOLE_H
#define SERIALCONSOLE_H

#include <QByteArray>
#include <QWidget>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;

// The dev unit's debug serial port as a terminal window: what the guest transmits scrolls by, and a typed line is sent
// back with Enter (docs/debug-protocol.md). It only shows and collects; EmulatorWidget owns the port.
class SerialConsole : public QWidget
{
    Q_OBJECT

public:
    explicit SerialConsole(QWidget *parent = nullptr);

    void appendOutput(const QByteArray &bytes);   // guest -> window
    void clear();

signals:
    void lineEntered(const QByteArray &bytes);    // window -> guest (the typed line, plus a newline if asked for)

private:
    QPlainTextEdit *log;
    QLineEdit *input;
    QCheckBox *newline;
};

#endif // SERIALCONSOLE_H
