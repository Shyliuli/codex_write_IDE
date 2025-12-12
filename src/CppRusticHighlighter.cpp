#include "CppRusticHighlighter.h"

#include <QTextDocument>
#include <QSettings>

#include <cstdio> // DEBUG_STARTUP

CppRusticHighlighter::CppRusticHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent) {
    std::fprintf(stderr, "[DEBUG_STARTUP] Highlighter ctor begin\n");
    std::fflush(stderr);
    const ColorScheme scheme = loadSchemeFromSettings();
    std::fprintf(stderr, "[DEBUG_STARTUP] Highlighter loadScheme done\n");
    std::fflush(stderr);
    setColorScheme(scheme);
    std::fprintf(stderr, "[DEBUG_STARTUP] Highlighter setColorScheme done\n");
    std::fflush(stderr);
}

void CppRusticHighlighter::setColorScheme(const ColorScheme &scheme) {
    std::fprintf(stderr, "[DEBUG_STARTUP] Highlighter setColorScheme begin\n");
    std::fflush(stderr);
    keywordFormat_.setForeground(scheme.keyword);
    keywordFormat_.setFontWeight(QFont::Bold);

    rusticKeywordFormat_.setForeground(scheme.rusticKeyword);
    rusticKeywordFormat_.setFontWeight(QFont::Bold);

    rusticTypeFormat_.setForeground(scheme.rusticType);

    functionFormat_.setForeground(scheme.function);
    functionFormat_.setFontWeight(QFont::Bold);

    preprocessorFormat_.setForeground(scheme.preprocessor);
    preprocessorFormat_.setFontWeight(QFont::Bold);

    singleLineCommentFormat_.setForeground(scheme.comment);
    multiLineCommentFormat_.setForeground(scheme.comment);

    quotationFormat_.setForeground(scheme.stringLiteral);

    numberFormat_.setForeground(scheme.number);

    std::fprintf(stderr, "[DEBUG_STARTUP] Highlighter building basic rules\n");
    std::fflush(stderr);
    buildBasicRules();
    std::fprintf(stderr, "[DEBUG_STARTUP] Highlighter building advanced rules\n");
    std::fflush(stderr);
    buildAdvancedRules();
    std::fprintf(stderr, "[DEBUG_STARTUP] Highlighter rehighlight begin\n");
    std::fflush(stderr);
    rehighlight();
    std::fprintf(stderr, "[DEBUG_STARTUP] Highlighter rehighlight done\n");
    std::fflush(stderr);
}

ColorScheme CppRusticHighlighter::colorScheme() const {
    ColorScheme s;
    s.keyword = keywordFormat_.foreground().color();
    s.rusticKeyword = rusticKeywordFormat_.foreground().color();
    s.rusticType = rusticTypeFormat_.foreground().color();
    s.function = functionFormat_.foreground().color();
    s.preprocessor = preprocessorFormat_.foreground().color();
    s.comment = singleLineCommentFormat_.foreground().color();
    s.stringLiteral = quotationFormat_.foreground().color();
    s.number = numberFormat_.foreground().color();
    return s;
}

ColorScheme CppRusticHighlighter::defaultScheme() {
    ColorScheme s;
    s.keyword = QColor(0, 70, 140);
    s.rusticKeyword = QColor(140, 0, 120);
    s.rusticType = QColor(0, 120, 80);
    s.function = QColor(20, 20, 20);
    s.preprocessor = QColor(0, 110, 0);
    s.comment = QColor(120, 120, 120);
    s.stringLiteral = QColor(170, 0, 0);
    s.number = QColor(120, 60, 0);
    return s;
}

ColorScheme CppRusticHighlighter::loadSchemeFromSettings() {
    QSettings settings(QStringLiteral("RusticCppIDE"), QStringLiteral("RusticCppIDE"));
    ColorScheme s = defaultScheme();

    auto read = [&settings](const QString &key, const QColor &fallback) {
        const QString val = settings.value(key).toString();
        return val.isEmpty() ? fallback : QColor(val);
    };

    s.keyword = read("colors/keyword", s.keyword);
    s.rusticKeyword = read("colors/rusticKeyword", s.rusticKeyword);
    s.rusticType = read("colors/rusticType", s.rusticType);
    s.function = read("colors/function", s.function);
    s.preprocessor = read("colors/preprocessor", s.preprocessor);
    s.comment = read("colors/comment", s.comment);
    s.stringLiteral = read("colors/string", s.stringLiteral);
    s.number = read("colors/number", s.number);
    return s;
}

void CppRusticHighlighter::saveSchemeToSettings(const ColorScheme &scheme) {
    QSettings settings(QStringLiteral("RusticCppIDE"), QStringLiteral("RusticCppIDE"));
    settings.setValue("colors/keyword", scheme.keyword.name());
    settings.setValue("colors/rusticKeyword", scheme.rusticKeyword.name());
    settings.setValue("colors/rusticType", scheme.rusticType.name());
    settings.setValue("colors/function", scheme.function.name());
    settings.setValue("colors/preprocessor", scheme.preprocessor.name());
    settings.setValue("colors/comment", scheme.comment.name());
    settings.setValue("colors/string", scheme.stringLiteral.name());
    settings.setValue("colors/number", scheme.number.name());
}

void CppRusticHighlighter::setAdvancedParsingEnabled(bool enabled) {
    if (advancedParsingEnabled_ == enabled) {
        return;
    }
    advancedParsingEnabled_ = enabled;
    rehighlight();
}

bool CppRusticHighlighter::advancedParsingEnabled() const {
    return advancedParsingEnabled_;
}

void CppRusticHighlighter::buildBasicRules() {
    basicRules_.clear();

    const QStringList cppKeywords = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool",
        "break", "case", "catch", "char", "char16_t", "char32_t", "class", "compl",
        "const", "constexpr", "const_cast", "continue", "decltype", "default", "delete",
        "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern",
        "false", "float", "for", "friend", "goto", "if", "inline", "int", "long",
        "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr",
        "operator", "or", "or_eq", "private", "protected", "public", "register",
        "reinterpret_cast", "return", "short", "signed", "sizeof", "static",
        "static_assert", "static_cast", "struct", "switch", "template", "this",
        "thread_local", "throw", "true", "try", "typedef", "typeid", "typename",
        "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t",
        "while", "xor", "xor_eq"};

    for (const QString &word : cppKeywords) {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("\\b%1\\b").arg(word));
        rule.format = keywordFormat_;
        basicRules_.append(rule);
    }

    const QStringList rusticKeywords = {
        "fn", "let", "let_mut", "trait", "impl", "from", "datafrom", "inner", "pub",
        "must", "def", "Case", "DefaultCase", "Ok", "Err", "Some", "None", "panic"};
    for (const QString &word : rusticKeywords) {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("\\b%1\\b").arg(word));
        rule.format = rusticKeywordFormat_;
        basicRules_.append(rule);
    }

    const QStringList rusticTypes = {
        "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64", "usize",
        "isize", "String", "Vec", "Option", "Result", "Unit"};
    for (const QString &word : rusticTypes) {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("\\b%1\\b").arg(word));
        rule.format = rusticTypeFormat_;
        basicRules_.append(rule);
    }

    Rule preprocessorRule;
    preprocessorRule.pattern = QRegularExpression(QStringLiteral("^\\s*#\\s*[a-zA-Z_]+"));
    preprocessorRule.format = preprocessorFormat_;
    basicRules_.append(preprocessorRule);

    Rule stringRule;
    stringRule.pattern = QRegularExpression(QStringLiteral("\"([^\\\\\"]|\\\\.)*\""));
    stringRule.format = quotationFormat_;
    basicRules_.append(stringRule);

    Rule charRule;
    charRule.pattern = QRegularExpression(QStringLiteral("'([^\\\\']|\\\\.)*'"));
    charRule.format = quotationFormat_;
    basicRules_.append(charRule);

    Rule numberRule;
    numberRule.pattern = QRegularExpression(QStringLiteral("\\b(0x[0-9A-Fa-f]+|\\d+(\\.\\d+)?)([uUlLfF]*)\\b"));
    numberRule.format = numberFormat_;
    basicRules_.append(numberRule);

    Rule singleLineCommentRule;
    singleLineCommentRule.pattern = QRegularExpression(QStringLiteral("//[^\\n]*"));
    singleLineCommentRule.format = singleLineCommentFormat_;
    basicRules_.append(singleLineCommentRule);
}

void CppRusticHighlighter::buildAdvancedRules() {
    advancedRules_.clear();

    Rule fnNameRule;
    fnNameRule.pattern = QRegularExpression(QStringLiteral("\\bfn\\s+([A-Za-z_][A-Za-z0-9_]*)"));
    fnNameRule.format = functionFormat_;
    fnNameRule.captureGroup = 1;
    advancedRules_.append(fnNameRule);

    Rule matchRule;
    matchRule.pattern = QRegularExpression(QStringLiteral("\\.match\\b"));
    matchRule.format = rusticKeywordFormat_;
    advancedRules_.append(matchRule);

    Rule arrowRule;
    arrowRule.pattern = QRegularExpression(QStringLiteral("->"));
    arrowRule.format = rusticKeywordFormat_;
    advancedRules_.append(arrowRule);
}

void CppRusticHighlighter::highlightBlock(const QString &text) {
    static bool debugOnce = true;
    const bool debug = debugOnce;
    if (debugOnce) {
        debugOnce = false;
        std::fprintf(stderr, "[DEBUG_STARTUP] highlightBlock first call, textLen=%d\n", text.size());
        std::fflush(stderr);
    }

    QVector<Rule> rulesToApply = basicRules_;
    if (advancedParsingEnabled_) {
        rulesToApply += advancedRules_;
    }

    for (int i = 0; i < rulesToApply.size(); ++i) {
        const Rule &rule = rulesToApply.at(i);
        if (debug) {
            std::fprintf(stderr, "[DEBUG_STARTUP] highlight rule %d/%d pattern='%s'\n",
                         i + 1,
                         rulesToApply.size(),
                         rule.pattern.pattern().toLocal8Bit().constData());
            std::fflush(stderr);
        }
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            const int start = match.capturedStart(rule.captureGroup);
            const int length = match.capturedLength(rule.captureGroup);
            if (start >= 0 && length > 0) {
                setFormat(start, length, rule.format);
            }
        }
        if (debug) {
            std::fprintf(stderr, "[DEBUG_STARTUP] highlight rule %d done\n", i + 1);
            std::fflush(stderr);
        }
    }

    setCurrentBlockState(0);

    int startIndex = 0;
    if (previousBlockState() != 1) {
        startIndex = text.indexOf(QStringLiteral("/*"));
    }

    while (startIndex >= 0) {
        const int endIndex = text.indexOf(QStringLiteral("*/"), startIndex + 2);
        int commentLength = 0;

        if (endIndex == -1) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex + 2;
        }

        setFormat(startIndex, commentLength, multiLineCommentFormat_);
        startIndex = text.indexOf(QStringLiteral("/*"), startIndex + commentLength);
    }

    if (debug) {
        std::fprintf(stderr, "[DEBUG_STARTUP] highlightBlock end\n");
        std::fflush(stderr);
    }
}
