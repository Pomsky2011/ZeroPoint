#ifndef KEYCAPTUREBUTTON_H
#define KEYCAPTUREBUTTON_H

#include <QPushButton>

// A button that, when clicked, grabs the keyboard and binds itself to the
// very next key pressed - including bare modifier keys like Shift, which
// this console's default mapping uses as an action button (Menu), and which
// widgets like QKeySequenceEdit refuse to capture on their own.
class KeyCaptureButton : public QPushButton
{
    Q_OBJECT

public:
    explicit KeyCaptureButton(QWidget *parent = nullptr);

    void setKey(int qtKey);
    int key() const { return boundKey; }

signals:
    void keyChanged(int qtKey);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private slots:
    void beginCapture();

private:
    int boundKey;
    bool capturing;
    void updateLabel();
};

#endif // KEYCAPTUREBUTTON_H
