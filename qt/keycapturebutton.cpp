#include "keycapturebutton.h"
#include <QKeyEvent>
#include <QKeySequence>

KeyCaptureButton::KeyCaptureButton(QWidget *parent)
    : QPushButton(parent)
    , boundKey(0)
    , capturing(false)
{
    connect(this, &QPushButton::clicked, this, &KeyCaptureButton::beginCapture);
    updateLabel();
}

void KeyCaptureButton::setKey(int qtKey)
{
    boundKey = qtKey;
    updateLabel();
}

void KeyCaptureButton::beginCapture()
{
    capturing = true;
    setText(tr("Press a key..."));
    grabKeyboard();
}

void KeyCaptureButton::keyPressEvent(QKeyEvent *event)
{
    if (!capturing) {
        QPushButton::keyPressEvent(event);
        return;
    }

    capturing = false;
    releaseKeyboard();

    // Escape cancels the capture without rebinding, matching common UX -
    // it was never one of this console's default bindings anyway.
    if (event->key() != Qt::Key_Escape) {
        boundKey = event->key();
        emit keyChanged(boundKey);
    }
    updateLabel();
    event->accept();
}

void KeyCaptureButton::focusOutEvent(QFocusEvent *event)
{
    if (capturing) {
        capturing = false;
        releaseKeyboard();
        updateLabel();
    }
    QPushButton::focusOutEvent(event);
}

void KeyCaptureButton::updateLabel()
{
    setText(boundKey ? QKeySequence(boundKey).toString() : tr("(unbound)"));
}
