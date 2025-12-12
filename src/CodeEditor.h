#pragma once

#include <QPlainTextEdit>
#include <QSet>

struct LspCompletionItem;

class LineNumberArea;
class QCompleter;
class QStandardItemModel;
class QKeyEvent;
class QMouseEvent;

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit CodeEditor(QWidget *parent = nullptr);

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);

    void setDiagnosticSelections(const QList<QTextEdit::ExtraSelection> &selections);
    void setSemanticSelections(const QList<QTextEdit::ExtraSelection> &selections);
    void setDebugSelections(const QList<QTextEdit::ExtraSelection> &selections);
    void showCompletions(const QList<LspCompletionItem> &items);

    void setBreakpoints(const QSet<int> &lines);
    QSet<int> breakpoints() const;

    void toggleBreakpointAtLine(int line);

    void setDarkThemeEnabled(bool enabled);

signals:
    void completionRequested(int line, int character);
    void gotoDefinitionRequested(int line, int character);
    void breakpointToggled(int line, bool enabled);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void highlightCurrentLine();
    void updateLineNumberArea(const QRect &rect, int dy);

private:
    LineNumberArea *lineNumberArea_;
    QList<QTextEdit::ExtraSelection> diagnosticSelections_;
    QList<QTextEdit::ExtraSelection> semanticSelections_;
    QList<QTextEdit::ExtraSelection> debugSelections_;
    QCompleter *completer_ = nullptr;
    QStandardItemModel *completionModel_ = nullptr;

    QSet<int> breakpoints_;
    bool darkThemeEnabled_ = false;

    void insertCompletion(const QString &completion);
    void insertCompletionFromIndex(const QModelIndex &index);
    bool isInCommentOrString(int positionInBlock) const;
    void addBracketMatchSelections(QList<QTextEdit::ExtraSelection> &selections);
    void indentSelection(int spaces);
    void unindentSelection(int spaces);
};

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor *editor);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    CodeEditor *editor_;
};
