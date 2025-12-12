#include "ProjectSettingsDialog.h"

#include "ProjectManager.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

ProjectSettingsDialog::ProjectSettingsDialog(ProjectManager *manager, QWidget *parent)
    : QDialog(parent), manager_(manager) {
    setWindowTitle(tr("工程设置"));
    setModal(true);

    compilerEdit_ = new QLineEdit(this);
    standardCombo_ = new QComboBox(this);
    standardCombo_->addItems({"c++17", "c++20", "c++23"});
    activeProfileCombo_ = new QComboBox(this);
    activeProfileCombo_->addItems({"Debug", "Release"});
    runArgsEdit_ = new QLineEdit(this);
    runDirEdit_ = new QLineEdit(this);

    auto *form = new QFormLayout();
    form->addRow(tr("编译器："), compilerEdit_);
    form->addRow(tr("C++ 标准："), standardCombo_);
    form->addRow(tr("当前编译模式："), activeProfileCombo_);
    form->addRow(tr("运行参数："), runArgsEdit_);
    form->addRow(tr("运行工作目录："), runDirEdit_);

    includeList_ = new QListWidget(this);
    auto *btnAddInc = new QPushButton(tr("添加目录..."), this);
    auto *btnRemoveInc = new QPushButton(tr("移除"), this);
    connect(btnAddInc, &QPushButton::clicked, this, &ProjectSettingsDialog::addIncludeDir);
    connect(btnRemoveInc, &QPushButton::clicked, this, &ProjectSettingsDialog::removeIncludeDir);

    auto *incBtnLayout = new QHBoxLayout();
    incBtnLayout->addWidget(btnAddInc);
    incBtnLayout->addWidget(btnRemoveInc);
    incBtnLayout->addStretch();

    auto *incGroup = new QGroupBox(tr("Include 目录"), this);
    auto *incLayout = new QVBoxLayout(incGroup);
    incLayout->addWidget(includeList_);
    incLayout->addLayout(incBtnLayout);

    flagsEdit_ = new QTextEdit(this);
    flagsEdit_->setPlaceholderText(tr("每行一个公共编译参数（Debug/Release 都会生效）\n例如：\n-DUSE_RUSTIC"));
    auto *flagsGroup = new QGroupBox(tr("公共编译参数"), this);
    auto *flagsLayout = new QVBoxLayout(flagsGroup);
    flagsLayout->addWidget(flagsEdit_);

    profileTabs_ = new QTabWidget(this);
    auto *debugTab = new QWidget(profileTabs_);
    auto *releaseTab = new QWidget(profileTabs_);

    debugOutputEdit_ = new QLineEdit(debugTab);
    debugFlagsEdit_ = new QTextEdit(debugTab);
    debugFlagsEdit_->setPlaceholderText(tr("每行一个 Debug 额外参数，例如：\n-g\n-O0"));
    auto *debugForm = new QFormLayout(debugTab);
    debugForm->addRow(tr("Debug 输出名："), debugOutputEdit_);
    debugForm->addRow(tr("Debug 额外参数："), debugFlagsEdit_);

    releaseOutputEdit_ = new QLineEdit(releaseTab);
    releaseFlagsEdit_ = new QTextEdit(releaseTab);
    releaseFlagsEdit_->setPlaceholderText(tr("每行一个 Release 额外参数，例如：\n-O2"));
    auto *releaseForm = new QFormLayout(releaseTab);
    releaseForm->addRow(tr("Release 输出名："), releaseOutputEdit_);
    releaseForm->addRow(tr("Release 额外参数："), releaseFlagsEdit_);

    profileTabs_->addTab(debugTab, tr("Debug"));
    profileTabs_->addTab(releaseTab, tr("Release"));
    auto *profilesGroup = new QGroupBox(tr("编译模式配置"), this);
    auto *profilesLayout = new QVBoxLayout(profilesGroup);
    profilesLayout->addWidget(profileTabs_);

    auto *btnOk = new QPushButton(tr("确定"), this);
    auto *btnCancel = new QPushButton(tr("取消"), this);
    connect(btnOk, &QPushButton::clicked, this, &ProjectSettingsDialog::applyAndClose);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(btnOk);
    btnLayout->addWidget(btnCancel);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(form);
    mainLayout->addWidget(incGroup);
    mainLayout->addWidget(flagsGroup);
    mainLayout->addWidget(profilesGroup);
    mainLayout->addLayout(btnLayout);

    loadFromProject();
}

void ProjectSettingsDialog::loadFromProject() {
    if (!manager_) {
        return;
    }
    compilerEdit_->setText(manager_->compiler());
    standardCombo_->setCurrentText(manager_->cxxStandard());
    activeProfileCombo_->setCurrentText(manager_->activeBuildProfile());
    runArgsEdit_->setText(manager_->runArgs().join(' '));
    runDirEdit_->setText(manager_->runWorkingDir());

    includeList_->clear();
    includeList_->addItems(manager_->includeDirs());

    flagsEdit_->setPlainText(manager_->extraFlags().join("\n"));

    const BuildProfile dbg = manager_->debugProfile();
    const BuildProfile rel = manager_->releaseProfile();
    debugOutputEdit_->setText(dbg.outputName);
    debugFlagsEdit_->setPlainText(dbg.flags.join("\n"));
    releaseOutputEdit_->setText(rel.outputName);
    releaseFlagsEdit_->setPlainText(rel.flags.join("\n"));

    if (manager_->activeBuildProfile().compare("Release", Qt::CaseInsensitive) == 0) {
        profileTabs_->setCurrentIndex(1);
    } else {
        profileTabs_->setCurrentIndex(0);
    }
}

void ProjectSettingsDialog::addIncludeDir() {
    if (!manager_) {
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(this, tr("选择 Include 目录"), manager_->rootDir());
    if (dir.isEmpty()) {
        return;
    }
    includeList_->addItem(dir);
}

void ProjectSettingsDialog::removeIncludeDir() {
    delete includeList_->takeItem(includeList_->currentRow());
}

void ProjectSettingsDialog::applyAndClose() {
    if (!manager_) {
        reject();
        return;
    }

    manager_->setCompiler(compilerEdit_->text());
    manager_->setCxxStandard(standardCombo_->currentText());
    manager_->setActiveBuildProfile(activeProfileCombo_->currentText());

    manager_->setRunArgs(runArgsEdit_->text().split(' ', Qt::SkipEmptyParts));
    manager_->setRunWorkingDir(runDirEdit_->text());

    QStringList dirs;
    for (int i = 0; i < includeList_->count(); ++i) {
        dirs.append(includeList_->item(i)->text());
    }
    manager_->setIncludeDirs(dirs);

    const QStringList flags = flagsEdit_->toPlainText().split('\n', Qt::SkipEmptyParts);
    manager_->setExtraFlags(flags);

    BuildProfile dbg = manager_->debugProfile();
    dbg.outputName = debugOutputEdit_->text().trimmed();
    dbg.flags = debugFlagsEdit_->toPlainText().split('\n', Qt::SkipEmptyParts);
    manager_->setDebugProfile(dbg);

    BuildProfile rel = manager_->releaseProfile();
    rel.outputName = releaseOutputEdit_->text().trimmed();
    rel.flags = releaseFlagsEdit_->toPlainText().split('\n', Qt::SkipEmptyParts);
    manager_->setReleaseProfile(rel);

    accept();
}
