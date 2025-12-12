#pragma once

#include <QSyntaxHighlighter>

#include <QRegularExpression>
#include <QTextCharFormat>
#include <QVector>

struct ColorScheme {
    QColor keyword;
    QColor rusticKeyword;
    QColor rusticType;
    QColor function;
    QColor preprocessor;
    QColor comment;
    QColor stringLiteral;
    QColor number;
};

class CppRusticHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit CppRusticHighlighter(QTextDocument *parent = nullptr);

    void setColorScheme(const ColorScheme &scheme);
    ColorScheme colorScheme() const;

    static ColorScheme defaultScheme();
    static ColorScheme loadSchemeFromSettings();
    static void saveSchemeToSettings(const ColorScheme &scheme);

    void setAdvancedParsingEnabled(bool enabled);
    bool advancedParsingEnabled() const;

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
        int captureGroup = 0;
    };

    void buildBasicRules();
    void buildAdvancedRules();

    QVector<Rule> basicRules_;
    QVector<Rule> advancedRules_;
    bool advancedParsingEnabled_ = false;

    QTextCharFormat keywordFormat_;
    QTextCharFormat rusticKeywordFormat_;
    QTextCharFormat rusticTypeFormat_;
    QTextCharFormat functionFormat_;
    QTextCharFormat preprocessorFormat_;
    QTextCharFormat singleLineCommentFormat_;
    QTextCharFormat multiLineCommentFormat_;
    QTextCharFormat quotationFormat_;
    QTextCharFormat numberFormat_;

};
