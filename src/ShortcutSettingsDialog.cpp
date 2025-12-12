#include "ShortcutSettingsDialog.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

ShortcutSettingsDialog::ShortcutSettingsDialog(const QList<QAction *> &actions, QWidget *parent)
    : QDialog(parent), actions_(actions) {
    setWindowTitle(tr("快捷键设置"));
    setModal(true);
    resize(520, 400);

    table_ = new QTableWidget(this);
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({tr("功能"), tr("快捷键")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setRowCount(actions_.size());

    for (int i = 0; i < actions_.size(); ++i) {
        QAction *act = actions_.at(i);
        auto *nameItem = new QTableWidgetItem(act->text().remove('&'));
        table_->setItem(i, 0, nameItem);

        auto *edit = new QKeySequenceEdit(act->shortcut(), table_);
        table_->setCellWidget(i, 1, edit);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ShortcutSettingsDialog::applyAndClose);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(table_);
    layout->addWidget(buttons);
}

void ShortcutSettingsDialog::applyAndClose() {
    QSettings settings(QStringLiteral("RusticCppIDE"), QStringLiteral("RusticCppIDE"));
    for (int i = 0; i < actions_.size(); ++i) {
        QAction *act = actions_.at(i);
        auto *edit = qobject_cast<QKeySequenceEdit *>(table_->cellWidget(i, 1));
        if (!edit) {
            continue;
        }
        const QKeySequence seq = edit->keySequence();
        act->setShortcut(seq);
        const QString key = act->objectName().isEmpty() ? act->text() : act->objectName();
        settings.setValue(QStringLiteral("shortcuts/") + key, seq.toString());
    }
    accept();
}

