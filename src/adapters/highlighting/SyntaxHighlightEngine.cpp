#include "SyntaxHighlightEngine.h"

#include <cstdint>

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QIODevice>
#include <QString>

#include <tree_sitter/api.h>

// Every vendored grammar (Architecture.md §6/§14.26 -- the public vcpkg registry only ships
// tree-sitter-c, the rest come from this repo's vcpkg-overlays/ports/tree-sitter-<lang> ports)
// exports its language table through a plain C entry point named tree_sitter_<lang>.
extern "C"
{
    const TSLanguage* tree_sitter_c(void);
    const TSLanguage* tree_sitter_cpp(void);
    const TSLanguage* tree_sitter_c_sharp(void);
    const TSLanguage* tree_sitter_python(void);
    const TSLanguage* tree_sitter_javascript(void);
    const TSLanguage* tree_sitter_typescript(void);
    const TSLanguage* tree_sitter_json(void);
    const TSLanguage* tree_sitter_html(void);
    const TSLanguage* tree_sitter_css(void);
}

namespace
{
    const TSLanguage* tsLanguageFor(Language language)
    {
        switch (language)
        {
            case Language::C:
                return tree_sitter_c();
            case Language::Cpp:
                return tree_sitter_cpp();
            case Language::CSharp:
                return tree_sitter_c_sharp();
            case Language::Python:
                return tree_sitter_python();
            case Language::JavaScript:
                return tree_sitter_javascript();
            case Language::TypeScript:
                return tree_sitter_typescript();
            case Language::Json:
                return tree_sitter_json();
            case Language::Html:
                return tree_sitter_html();
            case Language::Css:
                return tree_sitter_css();
        }
        return nullptr;
    }

    QString queryResourcePathFor(Language language)
    {
        switch (language)
        {
            case Language::C:
                return QStringLiteral(":/highlighting/c.scm");
            case Language::Cpp:
                return QStringLiteral(":/highlighting/cpp.scm");
            case Language::CSharp:
                return QStringLiteral(":/highlighting/csharp.scm");
            case Language::Python:
                return QStringLiteral(":/highlighting/python.scm");
            case Language::JavaScript:
                return QStringLiteral(":/highlighting/javascript.scm");
            case Language::TypeScript:
                return QStringLiteral(":/highlighting/typescript.scm");
            case Language::Json:
                return QStringLiteral(":/highlighting/json.scm");
            case Language::Html:
                return QStringLiteral(":/highlighting/html.scm");
            case Language::Css:
                return QStringLiteral(":/highlighting/css.scm");
        }
        return QString();
    }

    // The one Qt-touching function in this otherwise plain-C++ module (Architecture.md §14.26) --
    // reads a language's bundled highlights.scm query text out of the app's Qt resources (compiled
    // in from resources/tree-sitter-queries.qrc), the same mechanism the plan reserves for QML.
    std::string loadHighlightQuery(Language language)
    {
        QFile file(queryResourcePathFor(language));
        if (!file.open(QIODevice::ReadOnly))
        {
            return {};
        }
        const QByteArray bytes = file.readAll();
        return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    }
}

struct SyntaxHighlightEngine::LanguageContext
{
    TSParser* parser = nullptr;
    TSQuery* query = nullptr;
    std::vector<std::string> captureNames;

    ~LanguageContext()
    {
        if (query)
        {
            ts_query_delete(query);
        }
        if (parser)
        {
            ts_parser_delete(parser);
        }
    }
};

SyntaxHighlightEngine::SyntaxHighlightEngine()
{
    // The resource compiled from resources/tree-sitter-queries.qrc lives in this static library
    // (explorer_adapters_highlighting). A linker only pulls a static library's object file into the
    // final binary when something references a symbol it defines -- nothing here calls into the
    // qrc-generated translation unit directly, so without this the linker drops it entirely and
    // every QFile(":/highlighting/...") open below silently fails. Q_INIT_RESOURCE forces that
    // reference; it must be called from outside any C++ namespace, hence here rather than at the
    // point of use in loadHighlightQuery().
    Q_INIT_RESOURCE(tree_sitter_queries);
}

SyntaxHighlightEngine::~SyntaxHighlightEngine() = default;

SyntaxHighlightEngine::LanguageContext& SyntaxHighlightEngine::contextFor(Language language)
{
    auto existing = m_contexts.find(language);
    if (existing != m_contexts.end())
    {
        return *existing->second;
    }

    auto context = std::make_unique<LanguageContext>();
    const TSLanguage* tsLanguage = tsLanguageFor(language);
    const int languageIndex = static_cast<int>(language);

    context->parser = ts_parser_new();
    if (!ts_parser_set_language(context->parser, tsLanguage))
    {
        // Grammar/parser ABI mismatch (e.g. a vendored tree-sitter-<lang> port built against a
        // different tree-sitter core version) -- context->query stays null below, so highlight()
        // and captureNamesFor() permanently and silently return empty for this language without
        // this line, with nothing in the log to explain why.
        qWarning() << "SyntaxHighlightEngine: ts_parser_set_language failed for language index" << languageIndex
                    << "(grammar/tree-sitter ABI mismatch?)";
    }

    const std::string querySource = loadHighlightQuery(language);
    uint32_t errorOffset = 0;
    TSQueryError errorType = TSQueryErrorNone;
    context->query =
        ts_query_new(tsLanguage, querySource.c_str(), static_cast<uint32_t>(querySource.size()), &errorOffset, &errorType);

    if (!context->query)
    {
        qWarning() << "SyntaxHighlightEngine: ts_query_new failed for language index" << languageIndex << "at byte offset"
                    << errorOffset << "(TSQueryError" << static_cast<int>(errorType)
                    << ") -- highlighting for this language will stay empty; check the bundled .scm query for a syntax error";
    }

    if (context->query)
    {
        const uint32_t captureCount = ts_query_capture_count(context->query);
        context->captureNames.reserve(captureCount);
        for (uint32_t i = 0; i < captureCount; ++i)
        {
            uint32_t nameLength = 0;
            const char* name = ts_query_capture_name_for_id(context->query, i, &nameLength);
            context->captureNames.emplace_back(name, nameLength);
        }
    }

    auto [insertedIt, inserted] = m_contexts.emplace(language, std::move(context));
    (void)inserted;
    return *insertedIt->second;
}

std::vector<HighlightSpan> SyntaxHighlightEngine::highlight(Language language, std::string_view source)
{
    std::vector<HighlightSpan> spans;

    LanguageContext& context = contextFor(language);
    if (!context.query)
    {
        return spans;
    }

    TSTree* tree = ts_parser_parse_string(context.parser, nullptr, source.data(), static_cast<uint32_t>(source.size()));
    if (!tree)
    {
        return spans;
    }

    TSNode root = ts_tree_root_node(tree);
    TSQueryCursor* cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, context.query, root);

    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match))
    {
        for (uint16_t i = 0; i < match.capture_count; ++i)
        {
            const TSQueryCapture& capture = match.captures[i];
            uint32_t nameLength = 0;
            const char* name = ts_query_capture_name_for_id(context.query, capture.index, &nameLength);

            HighlightSpan span;
            span.byteOffset = ts_node_start_byte(capture.node);
            span.byteLength = ts_node_end_byte(capture.node) - span.byteOffset;
            span.captureName.assign(name, nameLength);
            spans.push_back(std::move(span));
        }
    }

    ts_query_cursor_delete(cursor);
    ts_tree_delete(tree);

    return spans;
}

const std::vector<std::string>& SyntaxHighlightEngine::captureNamesFor(Language language)
{
    return contextFor(language).captureNames;
}
