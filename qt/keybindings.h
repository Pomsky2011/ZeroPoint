#ifndef KEYBINDINGS_H
#define KEYBINDINGS_H

#include <Qt>

// Qt::Key values for each Player 1 input this frontend recognizes. Shared
// between ConfigDialog (which lets the user rebind them) and EmulatorWidget
// (which reads them in its key event handlers). See cpu.h PlayerInput:: for
// what each one maps to on the hardware side.
struct KeyBindings {
    int up = Qt::Key_Up;
    int down = Qt::Key_Down;
    int left = Qt::Key_Left;
    int right = Qt::Key_Right;

    int button1 = Qt::Key_Z;
    int button2 = Qt::Key_X;
    int button3 = Qt::Key_C;
    int button4 = Qt::Key_V;

    int bigLeft = Qt::Key_A;      // Left bumper
    int littleLeft = Qt::Key_S;   // Left trigger
    int littleRight = Qt::Key_D;  // Right trigger
    int bigRight = Qt::Key_F;     // Right bumper

    int menu = Qt::Key_Shift;
    int pause = Qt::Key_Return;
};

#endif // KEYBINDINGS_H
