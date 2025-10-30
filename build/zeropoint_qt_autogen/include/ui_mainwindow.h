/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.9.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QAction *actionLoadROM;
    QAction *actionExit;
    QAction *actionStart;
    QAction *actionStop;
    QAction *actionReset;
    QAction *actionConfiguration;
    QWidget *centralwidget;
    QVBoxLayout *verticalLayout;
    QLabel *statusLabel;
    QMenuBar *menubar;
    QMenu *menuFile;
    QMenu *menuEmulation;
    QMenu *menuSettings;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName("MainWindow");
        MainWindow->resize(800, 600);
        actionLoadROM = new QAction(MainWindow);
        actionLoadROM->setObjectName("actionLoadROM");
        actionExit = new QAction(MainWindow);
        actionExit->setObjectName("actionExit");
        actionStart = new QAction(MainWindow);
        actionStart->setObjectName("actionStart");
        actionStart->setEnabled(false);
        actionStop = new QAction(MainWindow);
        actionStop->setObjectName("actionStop");
        actionStop->setEnabled(false);
        actionReset = new QAction(MainWindow);
        actionReset->setObjectName("actionReset");
        actionReset->setEnabled(false);
        actionConfiguration = new QAction(MainWindow);
        actionConfiguration->setObjectName("actionConfiguration");
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName("centralwidget");
        verticalLayout = new QVBoxLayout(centralwidget);
        verticalLayout->setObjectName("verticalLayout");
        statusLabel = new QLabel(centralwidget);
        statusLabel->setObjectName("statusLabel");
        statusLabel->setAlignment(Qt::AlignCenter);

        verticalLayout->addWidget(statusLabel);

        MainWindow->setCentralWidget(centralwidget);
        menubar = new QMenuBar(MainWindow);
        menubar->setObjectName("menubar");
        menubar->setGeometry(QRect(0, 0, 800, 22));
        menuFile = new QMenu(menubar);
        menuFile->setObjectName("menuFile");
        menuEmulation = new QMenu(menubar);
        menuEmulation->setObjectName("menuEmulation");
        menuSettings = new QMenu(menubar);
        menuSettings->setObjectName("menuSettings");
        MainWindow->setMenuBar(menubar);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName("statusbar");
        MainWindow->setStatusBar(statusbar);

        menubar->addAction(menuFile->menuAction());
        menubar->addAction(menuEmulation->menuAction());
        menubar->addAction(menuSettings->menuAction());
        menuFile->addAction(actionLoadROM);
        menuFile->addSeparator();
        menuFile->addAction(actionExit);
        menuEmulation->addAction(actionStart);
        menuEmulation->addAction(actionStop);
        menuEmulation->addAction(actionReset);
        menuSettings->addAction(actionConfiguration);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "ZeroPoint Emulator", nullptr));
        actionLoadROM->setText(QCoreApplication::translate("MainWindow", "&Load ROM...", nullptr));
#if QT_CONFIG(shortcut)
        actionLoadROM->setShortcut(QCoreApplication::translate("MainWindow", "Ctrl+O", nullptr));
#endif // QT_CONFIG(shortcut)
        actionExit->setText(QCoreApplication::translate("MainWindow", "E&xit", nullptr));
#if QT_CONFIG(shortcut)
        actionExit->setShortcut(QCoreApplication::translate("MainWindow", "Ctrl+Q", nullptr));
#endif // QT_CONFIG(shortcut)
        actionStart->setText(QCoreApplication::translate("MainWindow", "&Start", nullptr));
        actionStop->setText(QCoreApplication::translate("MainWindow", "S&top", nullptr));
        actionReset->setText(QCoreApplication::translate("MainWindow", "&Reset", nullptr));
        actionConfiguration->setText(QCoreApplication::translate("MainWindow", "&Configuration...", nullptr));
        statusLabel->setText(QCoreApplication::translate("MainWindow", "No ROM loaded", nullptr));
        menuFile->setTitle(QCoreApplication::translate("MainWindow", "&File", nullptr));
        menuEmulation->setTitle(QCoreApplication::translate("MainWindow", "&Emulation", nullptr));
        menuSettings->setTitle(QCoreApplication::translate("MainWindow", "&Settings", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
