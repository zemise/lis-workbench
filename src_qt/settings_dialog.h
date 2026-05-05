#pragma once

#include "app_settings.h"

#include <QDialog>

class QLineEdit;
class QSpinBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const search::DbSettings& settings,
                            int fontSize,
                            QWidget* parent = nullptr);

    search::DbSettings dbSettings() const;
    int fontSize() const;

private slots:
    void onTestConnection();
    void onSave();

private:
    QLineEdit* server_ = nullptr;
    QLineEdit* database_ = nullptr;
    QLineEdit* user_ = nullptr;
    QLineEdit* password_ = nullptr;
    QSpinBox* fontSize_ = nullptr;
};
