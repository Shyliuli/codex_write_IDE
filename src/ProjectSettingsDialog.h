#pragma once

#include <QDialog>

class ProjectManager;
class QListWidget;
class QLineEdit;
class QComboBox;
class QTextEdit;
class QPushButton;
class QTabWidget;

class ProjectSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProjectSettingsDialog(ProjectManager *manager, QWidget *parent = nullptr);

private slots:
    void addIncludeDir();
    void removeIncludeDir();
    void applyAndClose();

private:
    void loadFromProject();

    ProjectManager *manager_;
    QLineEdit *compilerEdit_;
    QComboBox *standardCombo_;
    QComboBox *activeProfileCombo_;
    QLineEdit *debugOutputEdit_;
    QLineEdit *releaseOutputEdit_;
    QLineEdit *runArgsEdit_;
    QLineEdit *runDirEdit_;
    QListWidget *includeList_;
    QTextEdit *flagsEdit_;
    QTextEdit *debugFlagsEdit_;
    QTextEdit *releaseFlagsEdit_;
    QTabWidget *profileTabs_;
};
