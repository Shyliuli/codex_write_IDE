#include "CodeEditor.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QStyle>
#include <QTextBlock>

#include "LspClient.h"

LineNumberArea::LineNumberArea(CodeEditor *editor) : QWidget(editor), editor_(editor) {}

QSize LineNumberArea::sizeHint() const {
    return QSize(editor_->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent *event) {
    editor_->lineNumberAreaPaintEvent(event);
}

void LineNumberArea::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QTextCursor tc = editor_->cursorForPosition(QPoint(0, event->pos().y()));
        editor_->toggleBreakpointAtLine(tc.blockNumber());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent), lineNumberArea_(new LineNumberArea(this)) {
    connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &CodeEditor::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();

    QFont font;
    font.setFamily("Consolas");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(11);
    setFont(font);

    setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * 4);
    setLineWrapMode(QPlainTextEdit::NoWrap);
}

int CodeEditor::lineNumberAreaWidth() const {
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    const int space = 3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space + 14; // 预留断点区域，避免遮挡行号
}

void CodeEditor::updateLineNumberAreaWidth(int) {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy) {
    if (dy) {
        lineNumberArea_->scroll(0, dy);
    } else {
        lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(), rect.height());
    }

    if (rect.contains(viewport()->rect())) {
        updateLineNumberAreaWidth(0);
    }
}

void CodeEditor::resizeEvent(QResizeEvent *event) {
    QPlainTextEdit::resizeEvent(event);

    QRect cr = contentsRect();
    lineNumberArea_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extraSelections = semanticSelections_;
    extraSelections += diagnosticSelections_;
    extraSelections += debugSelections_;

    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        QColor lineColor = darkThemeEnabled_ ? QColor(60, 60, 60) : QColor(232, 242, 254);

        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }

    addBracketMatchSelections(extraSelections);

    setExtraSelections(extraSelections);
}

void CodeEditor::setDiagnosticSelections(const QList<QTextEdit::ExtraSelection> &selections) {
    diagnosticSelections_ = selections;
    highlightCurrentLine();
}

void CodeEditor::setSemanticSelections(const QList<QTextEdit::ExtraSelection> &selections) {
    semanticSelections_ = selections;
    highlightCurrentLine();
}

void CodeEditor::setDebugSelections(const QList<QTextEdit::ExtraSelection> &selections) {
    debugSelections_ = selections;
    highlightCurrentLine();
}

void CodeEditor::addBracketMatchSelections(QList<QTextEdit::ExtraSelection> &selections) {
    const QTextCursor cursor = textCursor();
    const int pos = cursor.position();
    const QString text = document()->toPlainText();
    if (text.isEmpty()) {
        return;
    }

    auto isBracket = [](QChar c) {
        return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}';
    };

    int bracketPos = -1;
    QChar bracketChar;
    if (pos > 0 && isBracket(text.at(pos - 1))) {
        bracketPos = pos - 1;
        bracketChar = text.at(bracketPos);
    } else if (pos < text.size() && isBracket(text.at(pos))) {
        bracketPos = pos;
        bracketChar = text.at(bracketPos);
    } else {
        return;
    }

    QChar matchChar;
    bool isOpen = false;
    if (bracketChar == '(') { matchChar = ')'; isOpen = true; }
    else if (bracketChar == '[') { matchChar = ']'; isOpen = true; }
    else if (bracketChar == '{') { matchChar = '}'; isOpen = true; }
    else if (bracketChar == ')') { matchChar = '('; isOpen = false; }
    else if (bracketChar == ']') { matchChar = '['; isOpen = false; }
    else if (bracketChar == '}') { matchChar = '{'; isOpen = false; }

    int matchPos = -1;
    int depth = 1;

    if (isOpen) {
        for (int i = bracketPos + 1; i < text.size(); ++i) {
            const QChar c = text.at(i);
            if (c == bracketChar) {
                ++depth;
            } else if (c == matchChar) {
                --depth;
                if (depth == 0) {
                    matchPos = i;
                    break;
                }
            }
        }
    } else {
        for (int i = bracketPos - 1; i >= 0; --i) {
            const QChar c = text.at(i);
            if (c == bracketChar) {
                ++depth;
            } else if (c == matchChar) {
                --depth;
                if (depth == 0) {
                    matchPos = i;
                    break;
                }
            }
        }
    }

    if (matchPos < 0) {
        return;
    }

    QTextCharFormat fmt;
    fmt.setBackground(QColor(255, 230, 150));
    fmt.setFontWeight(QFont::Bold);

    auto makeSelection = [&](int position) {
        QTextCursor c(document());
        c.setPosition(position);
        c.setPosition(position + 1, QTextCursor::KeepAnchor);
        QTextEdit::ExtraSelection sel;
        sel.cursor = c;
        sel.format = fmt;
        return sel;
    };

    selections.append(makeSelection(bracketPos));
    selections.append(makeSelection(matchPos));
}

void CodeEditor::indentSelection(int spaces) {
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection()) {
        return;
    }

    QTextBlock startBlock = document()->findBlock(cursor.selectionStart());
    QTextBlock endBlock = document()->findBlock(cursor.selectionEnd());

    cursor.beginEditBlock();
    QTextBlock block = startBlock;
    while (block.isValid()) {
        QTextCursor lineCursor(block);
        lineCursor.movePosition(QTextCursor::StartOfBlock);
        lineCursor.insertText(QString(spaces, ' '));
        if (block == endBlock) {
            break;
        }
        block = block.next();
    }
    cursor.endEditBlock();
}

void CodeEditor::unindentSelection(int spaces) {
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection()) {
        return;
    }

    QTextBlock startBlock = document()->findBlock(cursor.selectionStart());
    QTextBlock endBlock = document()->findBlock(cursor.selectionEnd());

    cursor.beginEditBlock();
    QTextBlock block = startBlock;
    while (block.isValid()) {
        QTextCursor lineCursor(block);
        lineCursor.movePosition(QTextCursor::StartOfBlock);
        const QString lineText = block.text();
        int removeCount = 0;
        while (removeCount < spaces && removeCount < lineText.size() && lineText.at(removeCount) == ' ') {
            ++removeCount;
        }
        if (removeCount > 0) {
            lineCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, removeCount);
            lineCursor.removeSelectedText();
        }
        if (block == endBlock) {
            break;
        }
        block = block.next();
    }
    cursor.endEditBlock();
}

void CodeEditor::insertCompletion(const QString &completion) {
    if (!completer_) {
        return;
    }
    QTextCursor tc = textCursor();
    tc.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, completer_->completionPrefix().length());
    tc.insertText(completion);
    setTextCursor(tc);
}

void CodeEditor::showCompletions(const QList<LspCompletionItem> &items) {
    if (!completionModel_) {
        completionModel_ = new QStandardItemModel(this);
    }
    completionModel_->clear();

    QTextCursor tc = textCursor();
    const QString prefix = tc.block().text().left(tc.positionInBlock()).split(QRegularExpression("\\W+")).last();
    const QString lowerPrefix = prefix.toLower();

    auto iconForKind = [] (int kind) -> QIcon {
        QStyle *style = QApplication::style();
        switch (kind) {
        case 2: // Method
        case 3: // Function
        case 4: // Constructor
            return style->standardIcon(QStyle::SP_ArrowRight);
        case 5: // Field
        case 6: // Variable
        case 21: // Constant
            return style->standardIcon(QStyle::SP_FileIcon);
        case 7: // Class
        case 22: // Struct
        case 13: // Enum
            return style->standardIcon(QStyle::SP_DirIcon);
        default:
            return style->standardIcon(QStyle::SP_MessageBoxInformation);
        }
    };

    for (const auto &item : items) {
        const QString insertText = item.insertText.isEmpty() ? item.label : item.insertText;
        if (!lowerPrefix.isEmpty()) {
            const QString lowerLabel = item.label.toLower();
            const QString lowerInsert = insertText.toLower();
            if (!lowerLabel.startsWith(lowerPrefix) && !lowerInsert.startsWith(lowerPrefix)) {
                continue;
            }
        }

        auto *row = new QStandardItem(item.label);
        row->setData(insertText, Qt::UserRole);
        row->setIcon(iconForKind(item.kind));
        completionModel_->appendRow(row);
    }

    if (!completer_) {
        completer_ = new QCompleter(completionModel_, this);
        completer_->setWidget(this);
        completer_->setCompletionMode(QCompleter::PopupCompletion);
        completer_->setCaseSensitivity(Qt::CaseInsensitive);
        completer_->setFilterMode(Qt::MatchStartsWith);
        connect(completer_, QOverload<const QModelIndex &>::of(&QCompleter::activated),
                this, &CodeEditor::insertCompletionFromIndex);
    } else {
        completer_->setModel(completionModel_);
    }
    completer_->setCompletionPrefix(prefix);
    QRect cr = cursorRect();
    cr.setWidth(completer_->popup()->sizeHintForColumn(0) + completer_->popup()->verticalScrollBar()->sizeHint().width());
    completer_->complete(cr);
}

void CodeEditor::insertCompletionFromIndex(const QModelIndex &index) {
    if (!completer_ || !index.isValid()) {
        return;
    }
    const QString insertText = completer_->completionModel()->data(index, Qt::UserRole).toString();
    insertCompletion(insertText);
}

bool CodeEditor::isInCommentOrString(int positionInBlock) const {
    const QTextBlock block = textCursor().block();
    if (block.userState() == 1) { // multi-line comment
        return true;
    }

    const QString text = block.text().left(positionInBlock);
    bool inString = false;
    bool inChar = false;
    bool escape = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        const QChar next = (i + 1 < text.size()) ? text.at(i + 1) : QChar();

        if (!inString && !inChar && c == '/' && next == '/') {
            return true; // single line comment
        }

        if (escape) {
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (!inChar && c == '"') {
            inString = !inString;
            continue;
        }
        if (!inString && c == '\'') {
            inChar = !inChar;
            continue;
        }
    }
    return inString || inChar;
}

void CodeEditor::setBreakpoints(const QSet<int> &lines) {
    breakpoints_ = lines;
    lineNumberArea_->update();
}

QSet<int> CodeEditor::breakpoints() const {
    return breakpoints_;
}

void CodeEditor::toggleBreakpointAtLine(int line) {
    if (breakpoints_.contains(line)) {
        breakpoints_.remove(line);
        emit breakpointToggled(line, false);
    } else {
        breakpoints_.insert(line);
        emit breakpointToggled(line, true);
    }
    lineNumberArea_->update();
}

void CodeEditor::setDarkThemeEnabled(bool enabled) {
    darkThemeEnabled_ = enabled;
    highlightCurrentLine();
    lineNumberArea_->update();
}

void CodeEditor::keyPressEvent(QKeyEvent *event) {
    if (completer_ && completer_->popup()->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Tab:
        case Qt::Key_Enter:
        case Qt::Key_Return: {
            insertCompletion(completer_->currentCompletion());
            completer_->popup()->hide();
            event->accept();
            return;
        }
        case Qt::Key_Escape:
            completer_->popup()->hide();
            event->accept();
            return;
        default:
            break;
        }
    }

    if (event->key() == Qt::Key_Tab && (!completer_ || !completer_->popup()->isVisible())) {
        QTextCursor tc = textCursor();
        if (tc.hasSelection()) {
            indentSelection(4);
        } else {
            insertPlainText(QString(4, ' '));
        }
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Backtab) {
        QTextCursor tc = textCursor();
        if (tc.hasSelection()) {
            unindentSelection(4);
        } else {
            QTextBlock block = tc.block();
            const QString lineText = block.text();
            int removeCount = 0;
            while (removeCount < 4 && removeCount < lineText.size() && lineText.at(removeCount) == ' ') {
                ++removeCount;
            }
            if (removeCount > 0) {
                QTextCursor lineCursor(block);
                lineCursor.movePosition(QTextCursor::StartOfBlock);
                lineCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, removeCount);
                lineCursor.removeSelectedText();
            }
        }
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        QTextCursor tc = textCursor();
        const QTextBlock currentBlock = tc.block();
        const QString prevText = currentBlock.text();

        QString indent;
        for (const QChar c : prevText) {
            if (c == ' ' || c == '\t') {
                indent.append(c);
            } else {
                break;
            }
        }

        const QString trimmed = prevText.trimmed();
        if (trimmed.endsWith('{')) {
            indent.append(QString(4, ' '));
        }

        QPlainTextEdit::keyPressEvent(event);
        tc = textCursor();
        tc.insertText(indent);
        setTextCursor(tc);
        return;
    }

    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_Space) {
        QTextCursor tc = textCursor();
        emit completionRequested(tc.blockNumber(), tc.positionInBlock());
        return;
    }

    QPlainTextEdit::keyPressEvent(event);

    if (event->text().isEmpty()) {
        return;
    }

    const QChar ch = event->text().at(0);
    const bool triggerChar = ch.isLetterOrNumber() || ch == '_' || ch == '.' || ch == ':' || ch == '>' || ch == '#';
    if (!triggerChar) {
        return;
    }

    QTextCursor tc = textCursor();
    if (!isInCommentOrString(tc.positionInBlock())) {
        emit completionRequested(tc.blockNumber(), tc.positionInBlock());
    }
}

void CodeEditor::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
        QTextCursor tc = cursorForPosition(event->pos());
        emit gotoDefinitionRequested(tc.blockNumber(), tc.positionInBlock());
        event->accept();
        return;
    }
    QPlainTextEdit::mousePressEvent(event);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event) {
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), darkThemeEnabled_ ? QColor(45, 45, 45) : QColor(245, 245, 245));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            painter.setPen(darkThemeEnabled_ ? QColor(180, 180, 180) : Qt::gray);
            painter.drawText(0, top, lineNumberArea_->width() - 5, fontMetrics().height(), Qt::AlignRight, number);

            if (breakpoints_.contains(blockNumber)) {
                const int radius = 5;
                const int centerY = top + fontMetrics().height() / 2;
                painter.setBrush(QColor(200, 0, 0));
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(QPoint(radius + 2, centerY), radius, radius);
            }
        }

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}
