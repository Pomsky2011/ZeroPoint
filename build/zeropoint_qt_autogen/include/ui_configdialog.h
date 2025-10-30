/********************************************************************************
** Form generated from reading UI file 'configdialog.ui'
**
** Created by: Qt User Interface Compiler version 6.9.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_CONFIGDIALOG_H
#define UI_CONFIGDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_ConfigDialog
{
public:
    QVBoxLayout *verticalLayout;
    QTabWidget *tabWidget;
    QWidget *displayTab;
    QFormLayout *formLayout;
    QLabel *scaleLabel;
    QSpinBox *scaleSpinBox;
    QLabel *vsyncLabel;
    QCheckBox *vsyncCheckBox;
    QLabel *filterLabel;
    QComboBox *filterComboBox;
    QWidget *audioTab;
    QFormLayout *formLayout_2;
    QLabel *enableAudioLabel;
    QCheckBox *enableAudioCheckBox;
    QLabel *volumeLabel;
    QSlider *volumeSlider;
    QWidget *inputTab;
    QVBoxLayout *verticalLayout_3;
    QLabel *inputInfoLabel;
    QGroupBox *keyboardGroupBox;
    QFormLayout *formLayout_3;
    QLabel *upLabel;
    QLineEdit *upKeyEdit;
    QLabel *downLabel;
    QLineEdit *downKeyEdit;
    QLabel *leftLabel;
    QLineEdit *leftKeyEdit;
    QLabel *rightLabel;
    QLineEdit *rightKeyEdit;
    QLabel *aButtonLabel;
    QLineEdit *aButtonEdit;
    QLabel *bButtonLabel;
    QLineEdit *bButtonEdit;
    QLabel *startLabel;
    QLineEdit *startKeyEdit;
    QLabel *selectLabel;
    QLineEdit *selectKeyEdit;
    QSpacerItem *verticalSpacer;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *ConfigDialog)
    {
        if (ConfigDialog->objectName().isEmpty())
            ConfigDialog->setObjectName("ConfigDialog");
        ConfigDialog->resize(500, 400);
        verticalLayout = new QVBoxLayout(ConfigDialog);
        verticalLayout->setObjectName("verticalLayout");
        tabWidget = new QTabWidget(ConfigDialog);
        tabWidget->setObjectName("tabWidget");
        displayTab = new QWidget();
        displayTab->setObjectName("displayTab");
        formLayout = new QFormLayout(displayTab);
        formLayout->setObjectName("formLayout");
        scaleLabel = new QLabel(displayTab);
        scaleLabel->setObjectName("scaleLabel");

        formLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, scaleLabel);

        scaleSpinBox = new QSpinBox(displayTab);
        scaleSpinBox->setObjectName("scaleSpinBox");
        scaleSpinBox->setMinimum(1);
        scaleSpinBox->setMaximum(4);
        scaleSpinBox->setValue(2);

        formLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, scaleSpinBox);

        vsyncLabel = new QLabel(displayTab);
        vsyncLabel->setObjectName("vsyncLabel");

        formLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, vsyncLabel);

        vsyncCheckBox = new QCheckBox(displayTab);
        vsyncCheckBox->setObjectName("vsyncCheckBox");
        vsyncCheckBox->setChecked(true);

        formLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, vsyncCheckBox);

        filterLabel = new QLabel(displayTab);
        filterLabel->setObjectName("filterLabel");

        formLayout->setWidget(2, QFormLayout::ItemRole::LabelRole, filterLabel);

        filterComboBox = new QComboBox(displayTab);
        filterComboBox->addItem(QString());
        filterComboBox->addItem(QString());
        filterComboBox->setObjectName("filterComboBox");

        formLayout->setWidget(2, QFormLayout::ItemRole::FieldRole, filterComboBox);

        tabWidget->addTab(displayTab, QString());
        audioTab = new QWidget();
        audioTab->setObjectName("audioTab");
        formLayout_2 = new QFormLayout(audioTab);
        formLayout_2->setObjectName("formLayout_2");
        enableAudioLabel = new QLabel(audioTab);
        enableAudioLabel->setObjectName("enableAudioLabel");

        formLayout_2->setWidget(0, QFormLayout::ItemRole::LabelRole, enableAudioLabel);

        enableAudioCheckBox = new QCheckBox(audioTab);
        enableAudioCheckBox->setObjectName("enableAudioCheckBox");
        enableAudioCheckBox->setChecked(true);

        formLayout_2->setWidget(0, QFormLayout::ItemRole::FieldRole, enableAudioCheckBox);

        volumeLabel = new QLabel(audioTab);
        volumeLabel->setObjectName("volumeLabel");

        formLayout_2->setWidget(1, QFormLayout::ItemRole::LabelRole, volumeLabel);

        volumeSlider = new QSlider(audioTab);
        volumeSlider->setObjectName("volumeSlider");
        volumeSlider->setMaximum(100);
        volumeSlider->setValue(80);
        volumeSlider->setOrientation(Qt::Horizontal);

        formLayout_2->setWidget(1, QFormLayout::ItemRole::FieldRole, volumeSlider);

        tabWidget->addTab(audioTab, QString());
        inputTab = new QWidget();
        inputTab->setObjectName("inputTab");
        verticalLayout_3 = new QVBoxLayout(inputTab);
        verticalLayout_3->setObjectName("verticalLayout_3");
        inputInfoLabel = new QLabel(inputTab);
        inputInfoLabel->setObjectName("inputInfoLabel");

        verticalLayout_3->addWidget(inputInfoLabel);

        keyboardGroupBox = new QGroupBox(inputTab);
        keyboardGroupBox->setObjectName("keyboardGroupBox");
        formLayout_3 = new QFormLayout(keyboardGroupBox);
        formLayout_3->setObjectName("formLayout_3");
        upLabel = new QLabel(keyboardGroupBox);
        upLabel->setObjectName("upLabel");

        formLayout_3->setWidget(0, QFormLayout::ItemRole::LabelRole, upLabel);

        upKeyEdit = new QLineEdit(keyboardGroupBox);
        upKeyEdit->setObjectName("upKeyEdit");
        upKeyEdit->setReadOnly(true);

        formLayout_3->setWidget(0, QFormLayout::ItemRole::FieldRole, upKeyEdit);

        downLabel = new QLabel(keyboardGroupBox);
        downLabel->setObjectName("downLabel");

        formLayout_3->setWidget(1, QFormLayout::ItemRole::LabelRole, downLabel);

        downKeyEdit = new QLineEdit(keyboardGroupBox);
        downKeyEdit->setObjectName("downKeyEdit");
        downKeyEdit->setReadOnly(true);

        formLayout_3->setWidget(1, QFormLayout::ItemRole::FieldRole, downKeyEdit);

        leftLabel = new QLabel(keyboardGroupBox);
        leftLabel->setObjectName("leftLabel");

        formLayout_3->setWidget(2, QFormLayout::ItemRole::LabelRole, leftLabel);

        leftKeyEdit = new QLineEdit(keyboardGroupBox);
        leftKeyEdit->setObjectName("leftKeyEdit");
        leftKeyEdit->setReadOnly(true);

        formLayout_3->setWidget(2, QFormLayout::ItemRole::FieldRole, leftKeyEdit);

        rightLabel = new QLabel(keyboardGroupBox);
        rightLabel->setObjectName("rightLabel");

        formLayout_3->setWidget(3, QFormLayout::ItemRole::LabelRole, rightLabel);

        rightKeyEdit = new QLineEdit(keyboardGroupBox);
        rightKeyEdit->setObjectName("rightKeyEdit");
        rightKeyEdit->setReadOnly(true);

        formLayout_3->setWidget(3, QFormLayout::ItemRole::FieldRole, rightKeyEdit);

        aButtonLabel = new QLabel(keyboardGroupBox);
        aButtonLabel->setObjectName("aButtonLabel");

        formLayout_3->setWidget(4, QFormLayout::ItemRole::LabelRole, aButtonLabel);

        aButtonEdit = new QLineEdit(keyboardGroupBox);
        aButtonEdit->setObjectName("aButtonEdit");
        aButtonEdit->setReadOnly(true);

        formLayout_3->setWidget(4, QFormLayout::ItemRole::FieldRole, aButtonEdit);

        bButtonLabel = new QLabel(keyboardGroupBox);
        bButtonLabel->setObjectName("bButtonLabel");

        formLayout_3->setWidget(5, QFormLayout::ItemRole::LabelRole, bButtonLabel);

        bButtonEdit = new QLineEdit(keyboardGroupBox);
        bButtonEdit->setObjectName("bButtonEdit");
        bButtonEdit->setReadOnly(true);

        formLayout_3->setWidget(5, QFormLayout::ItemRole::FieldRole, bButtonEdit);

        startLabel = new QLabel(keyboardGroupBox);
        startLabel->setObjectName("startLabel");

        formLayout_3->setWidget(6, QFormLayout::ItemRole::LabelRole, startLabel);

        startKeyEdit = new QLineEdit(keyboardGroupBox);
        startKeyEdit->setObjectName("startKeyEdit");
        startKeyEdit->setReadOnly(true);

        formLayout_3->setWidget(6, QFormLayout::ItemRole::FieldRole, startKeyEdit);

        selectLabel = new QLabel(keyboardGroupBox);
        selectLabel->setObjectName("selectLabel");

        formLayout_3->setWidget(7, QFormLayout::ItemRole::LabelRole, selectLabel);

        selectKeyEdit = new QLineEdit(keyboardGroupBox);
        selectKeyEdit->setObjectName("selectKeyEdit");
        selectKeyEdit->setReadOnly(true);

        formLayout_3->setWidget(7, QFormLayout::ItemRole::FieldRole, selectKeyEdit);


        verticalLayout_3->addWidget(keyboardGroupBox);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        verticalLayout_3->addItem(verticalSpacer);

        tabWidget->addTab(inputTab, QString());

        verticalLayout->addWidget(tabWidget);

        buttonBox = new QDialogButtonBox(ConfigDialog);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(ConfigDialog);
        QObject::connect(buttonBox, &QDialogButtonBox::accepted, ConfigDialog, qOverload<>(&QDialog::accept));
        QObject::connect(buttonBox, &QDialogButtonBox::rejected, ConfigDialog, qOverload<>(&QDialog::reject));

        tabWidget->setCurrentIndex(0);


        QMetaObject::connectSlotsByName(ConfigDialog);
    } // setupUi

    void retranslateUi(QDialog *ConfigDialog)
    {
        ConfigDialog->setWindowTitle(QCoreApplication::translate("ConfigDialog", "Configuration", nullptr));
        scaleLabel->setText(QCoreApplication::translate("ConfigDialog", "Window Scale:", nullptr));
        vsyncLabel->setText(QCoreApplication::translate("ConfigDialog", "VSync:", nullptr));
        filterLabel->setText(QCoreApplication::translate("ConfigDialog", "Filtering:", nullptr));
        filterComboBox->setItemText(0, QCoreApplication::translate("ConfigDialog", "Nearest (Sharp)", nullptr));
        filterComboBox->setItemText(1, QCoreApplication::translate("ConfigDialog", "Linear (Smooth)", nullptr));

        tabWidget->setTabText(tabWidget->indexOf(displayTab), QCoreApplication::translate("ConfigDialog", "Display", nullptr));
        enableAudioLabel->setText(QCoreApplication::translate("ConfigDialog", "Enable Audio:", nullptr));
        volumeLabel->setText(QCoreApplication::translate("ConfigDialog", "Volume:", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(audioTab), QCoreApplication::translate("ConfigDialog", "Audio", nullptr));
        inputInfoLabel->setText(QCoreApplication::translate("ConfigDialog", "Keyboard mappings for controller buttons:", nullptr));
        keyboardGroupBox->setTitle(QCoreApplication::translate("ConfigDialog", "Keyboard Controls", nullptr));
        upLabel->setText(QCoreApplication::translate("ConfigDialog", "Up:", nullptr));
        upKeyEdit->setText(QCoreApplication::translate("ConfigDialog", "Up Arrow", nullptr));
        downLabel->setText(QCoreApplication::translate("ConfigDialog", "Down:", nullptr));
        downKeyEdit->setText(QCoreApplication::translate("ConfigDialog", "Down Arrow", nullptr));
        leftLabel->setText(QCoreApplication::translate("ConfigDialog", "Left:", nullptr));
        leftKeyEdit->setText(QCoreApplication::translate("ConfigDialog", "Left Arrow", nullptr));
        rightLabel->setText(QCoreApplication::translate("ConfigDialog", "Right:", nullptr));
        rightKeyEdit->setText(QCoreApplication::translate("ConfigDialog", "Right Arrow", nullptr));
        aButtonLabel->setText(QCoreApplication::translate("ConfigDialog", "A Button:", nullptr));
        aButtonEdit->setText(QCoreApplication::translate("ConfigDialog", "Z", nullptr));
        bButtonLabel->setText(QCoreApplication::translate("ConfigDialog", "B Button:", nullptr));
        bButtonEdit->setText(QCoreApplication::translate("ConfigDialog", "X", nullptr));
        startLabel->setText(QCoreApplication::translate("ConfigDialog", "Start:", nullptr));
        startKeyEdit->setText(QCoreApplication::translate("ConfigDialog", "Return", nullptr));
        selectLabel->setText(QCoreApplication::translate("ConfigDialog", "Select:", nullptr));
        selectKeyEdit->setText(QCoreApplication::translate("ConfigDialog", "Right Shift", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(inputTab), QCoreApplication::translate("ConfigDialog", "Input", nullptr));
    } // retranslateUi

};

namespace Ui {
    class ConfigDialog: public Ui_ConfigDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_CONFIGDIALOG_H
