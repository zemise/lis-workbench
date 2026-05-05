#include "settings_dialog.h"
#include "search_controller.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(const search::DbSettings& settings,
                               int fontSize,
                               QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QString::fromWCharArray(L"数据库设置"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setMinimumWidth(460);
    setModal(true);

    server_ = new QLineEdit(this);
    server_->setText(QString::fromStdWString(settings.server));
    server_->setPlaceholderText("192.168.1.100\\MSSQLSERVER1");

    database_ = new QLineEdit(this);
    database_->setText(QString::fromStdWString(settings.initial_database));
    database_->setPlaceholderText("trasen");

    user_ = new QLineEdit(this);
    user_->setText(QString::fromStdWString(settings.user));
    user_->setPlaceholderText("sa");

    password_ = new QLineEdit(this);
    password_->setEchoMode(QLineEdit::Password);
    password_->setText(QString::fromStdWString(settings.password));

    fontSize_ = new QSpinBox(this);
    fontSize_->setRange(8, 24);
    fontSize_->setValue(fontSize);
    fontSize_->setSuffix(" pt");

    auto* form = new QFormLayout;
    form->addRow(QString::fromWCharArray(L"服务器"), server_);
    form->addRow(QString::fromWCharArray(L"初始数据库"), database_);
    form->addRow(QString::fromWCharArray(L"用户名"), user_);
    form->addRow(QString::fromWCharArray(L"密码"), password_);
    form->addRow(QString::fromWCharArray(L"字号"), fontSize_);

    auto* testBtn = new QPushButton(QString::fromWCharArray(L"测试连接"), this);
    auto* saveBtn = new QPushButton(QString::fromWCharArray(L"保存"), this);
    auto* cancelBtn = new QPushButton(QString::fromWCharArray(L"取消"), this);
    saveBtn->setDefault(true);

    auto* btnLayout = new QHBoxLayout;
    btnLayout->addWidget(testBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(saveBtn);
    btnLayout->addWidget(cancelBtn);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(form);
    mainLayout->addSpacing(8);
    mainLayout->addLayout(btnLayout);

    connect(testBtn, &QPushButton::clicked, this, &SettingsDialog::onTestConnection);
    connect(saveBtn, &QPushButton::clicked, this, &SettingsDialog::onSave);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    // Apply font size to the dialog itself
    QFont f = font();
    f.setPointSize(fontSize);
    setFont(f);
}

search::DbSettings SettingsDialog::dbSettings() const {
    search::DbSettings s;
    s.server = server_->text().toStdWString();
    s.initial_database = database_->text().toStdWString();
    s.user = user_->text().toStdWString();
    s.password = password_->text().toStdWString();
    return s;
}

int SettingsDialog::fontSize() const {
    return fontSize_->value();
}

void SettingsDialog::onTestConnection() {
    const auto settings = dbSettings();
    if (settings.server.empty() || settings.initial_database.empty() || settings.user.empty()) {
        QMessageBox::warning(this,
                             QString::fromWCharArray(L"测试连接"),
                             QString::fromWCharArray(L"请先填写服务器、初始数据库和用户名。"));
        return;
    }
    std::string error;
    if (search::test_database_connection(settings, error)) {
        QMessageBox::information(this,
                                 QString::fromWCharArray(L"测试连接"),
                                 QString::fromWCharArray(L"数据库连接成功。"));
    } else {
        QMessageBox::critical(this,
                              QString::fromWCharArray(L"数据库连接失败"),
                              QString::fromStdString(error));
    }
}

void SettingsDialog::onSave() {
    const auto settings = dbSettings();
    if (settings.server.empty() || settings.initial_database.empty() || settings.user.empty()) {
        QMessageBox::warning(this,
                             QString::fromWCharArray(L"保存"),
                             QString::fromWCharArray(L"请先填写服务器、初始数据库和用户名。"));
        return;
    }
    accept();
}
