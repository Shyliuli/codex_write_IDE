#pragma once

#include <QDialog>

class CodeEditor;
class QCheckBox;
class QLineEdit;

class FindReplaceDialog : public QDialog {
    Q_OBJECT

public:
    explicit FindReplaceDialog(QWidget *parent = nullptr);

    void setEditor(CodeEditor *editor);
    void showFind();
    void showReplace();

private slots:
    void findNext();
    void replaceOne();
    void replaceAll();

private:
    CodeEditor *editor_ = nullptr;
    QLineEdit *findEdit_ = nullptr;
    QLineEdit *replaceEdit_ = nullptr;
    QCheckBox *caseSensitiveBox_ = nullptr;
};

