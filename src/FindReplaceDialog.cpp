#include "FindReplaceDialog.h"

#include "CodeEditor.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

FindReplaceDialog::FindReplaceDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("查找/替换"));
    setModal(false);

    findEdit_ = new QLineEdit(this);
    replaceEdit_ = new QLineEdit(this);
    caseSensitiveBox_ = new QCheckBox(tr("区分大小写"), this);

    auto *form = new QFormLayout();
    form->addRow(tr("查找："), findEdit_);
    form->addRow(tr("替换为："), replaceEdit_);
    form->addRow(QString(), caseSensitiveBox_);

    auto *btnFind = new QPushButton(tr("查找下一个"), this);
    auto *btnReplace = new QPushButton(tr("替换"), this);
    auto *btnReplaceAll = new QPushButton(tr("全部替换"), this);
    auto *btnClose = new QPushButton(tr("关闭"), this);

    connect(btnFind, &QPushButton::clicked, this, &FindReplaceDialog::findNext);
    connect(btnReplace, &QPushButton::clicked, this, &FindReplaceDialog::replaceOne);
    connect(btnReplaceAll, &QPushButton::clicked, this, &FindReplaceDialog::replaceAll);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::hide);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addWidget(btnFind);
    btnLayout->addWidget(btnReplace);
    btnLayout->addWidget(btnReplaceAll);
    btnLayout->addStretch();
    btnLayout->addWidget(btnClose);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(form);
    mainLayout->addLayout(btnLayout);
}

void FindReplaceDialog::setEditor(CodeEditor *editor) {
    editor_ = editor;
}

void FindReplaceDialog::showFind() {
    if (replaceEdit_) {
        replaceEdit_->setVisible(false);
    }
    setWindowTitle(tr("查找"));
    show();
    raise();
    activateWindow();
    findEdit_->setFocus();
    findEdit_->selectAll();
}

void FindReplaceDialog::showReplace() {
    if (replaceEdit_) {
        replaceEdit_->setVisible(true);
    }
    setWindowTitle(tr("替换"));
    show();
    raise();
    activateWindow();
    findEdit_->setFocus();
    findEdit_->selectAll();
}

void FindReplaceDialog::findNext() {
    if (!editor_) {
        return;
    }

    const QString query = findEdit_->text();
    if (query.isEmpty()) {
        return;
    }

    QTextDocument::FindFlags flags;
    if (caseSensitiveBox_->isChecked()) {
        flags |= QTextDocument::FindCaseSensitively;
    }

    if (!editor_->find(query, flags)) {
        QTextCursor cursor = editor_->textCursor();
        cursor.movePosition(QTextCursor::Start);
        editor_->setTextCursor(cursor);
        editor_->find(query, flags);
    }
}

void FindReplaceDialog::replaceOne() {
    if (!editor_) {
        return;
    }

    const QString query = findEdit_->text();
    if (query.isEmpty()) {
        return;
    }

    QTextCursor cursor = editor_->textCursor();
    if (cursor.hasSelection() && cursor.selectedText() == query) {
        cursor.insertText(replaceEdit_->text());
        editor_->setTextCursor(cursor);
    }
    findNext();
}

void FindReplaceDialog::replaceAll() {
    if (!editor_) {
        return;
    }

    const QString query = findEdit_->text();
    if (query.isEmpty()) {
        return;
    }

    QTextDocument::FindFlags flags;
    if (caseSensitiveBox_->isChecked()) {
        flags |= QTextDocument::FindCaseSensitively;
    }

    QTextCursor cursor(editor_->document());
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::Start);
    editor_->setTextCursor(cursor);

    while (editor_->find(query, flags)) {
        QTextCursor c = editor_->textCursor();
        c.insertText(replaceEdit_->text());
        editor_->setTextCursor(c);
    }
    cursor.endEditBlock();
}
