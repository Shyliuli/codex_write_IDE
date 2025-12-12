#pragma once

#include <QDialog>

#include <QList>

class QAction;
class QTableWidget;

class ShortcutSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ShortcutSettingsDialog(const QList<QAction *> &actions, QWidget *parent = nullptr);

private slots:
    void applyAndClose();

private:
    QList<QAction *> actions_;
    QTableWidget *table_ = nullptr;
};

