#include "ContentSearchResultsModel.h"

#include <optional>
#include <utility>

#include "PathUtf8.h"

namespace
{
    QString toQString(const std::string& utf8)
    {
        return QString::fromStdString(utf8);
    }

    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }
}

struct ContentSearchResultsModel::Node
{
    Node* parent = nullptr;
    int row = 0;
    std::vector<std::unique_ptr<Node>> children; // only header rows have any

    // Header-row fields. FileNode has no default constructor (identity is always explicit), so
    // this is an optional rather than a plain value -- only ever unset for the invisible root node
    // and for line rows, neither of which this field is ever read from.
    std::optional<FileNode> file;
    QString displayText;

    // Line-row fields (lineNumber == 0 marks a header row).
    int lineNumber = 0;
    std::string lineTextUtf8;
};

ContentSearchResultsModel::ContentSearchResultsModel(QObject* parent)
    : QAbstractItemModel(parent)
    , m_root(std::make_unique<Node>())
{
}

ContentSearchResultsModel::~ContentSearchResultsModel() = default;

ContentSearchResultsModel::Node* ContentSearchResultsModel::nodeFor(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<Node*>(index.internalPointer()) : m_root.get();
}

QModelIndex ContentSearchResultsModel::indexFor(const Node* node) const
{
    if (node == nullptr || node == m_root.get())
    {
        return QModelIndex();
    }
    return createIndex(node->row, 0, const_cast<Node*>(node));
}

QModelIndex ContentSearchResultsModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0 || row < 0)
    {
        return QModelIndex();
    }

    const Node* parentNode = nodeFor(parent);
    if (static_cast<std::size_t>(row) >= parentNode->children.size())
    {
        return QModelIndex();
    }
    return createIndex(row, 0, parentNode->children[static_cast<std::size_t>(row)].get());
}

QModelIndex ContentSearchResultsModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
    {
        return QModelIndex();
    }
    return indexFor(nodeFor(child)->parent);
}

int ContentSearchResultsModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0)
    {
        return 0;
    }
    return static_cast<int>(nodeFor(parent)->children.size());
}

int ContentSearchResultsModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant ContentSearchResultsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    const Node* node = nodeFor(index);
    const bool isHeaderRow = node->lineNumber == 0;

    switch (role)
    {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return isHeaderRow ? node->displayText
                                : QStringLiteral("%1:  %2").arg(node->lineNumber).arg(toQString(node->lineTextUtf8));
        case PathRole:
            return toQString((isHeaderRow ? node : node->parent)->file->path());
        case IsDirectoryRole:
            return false;
        case IsHiddenRole:
            return (isHeaderRow ? node : node->parent)->file->isHidden();
        case LineNumberRole:
            return isHeaderRow ? QVariant() : node->lineNumber;
        case FullLineTextRole:
            return isHeaderRow ? QVariant() : toQString(node->lineTextUtf8);
        default:
            return QVariant();
    }
}

Qt::ItemFlags ContentSearchResultsModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void ContentSearchResultsModel::setResults(const std::vector<FileContentMatch>& matches, const std::filesystem::path& root)
{
    beginResetModel();

    m_root = std::make_unique<Node>();
    m_matchCount = 0;
    m_fileCount = static_cast<int>(matches.size());

    for (const FileContentMatch& match : matches)
    {
        auto headerNode = std::make_unique<Node>();
        headerNode->parent = m_root.get();
        headerNode->row = static_cast<int>(m_root->children.size());
        headerNode->file = match.node;

        // Path context to disambiguate same-named files in different subfolders: the file's
        // directory relative to the search root, inline next to the name rather than a folder
        // hierarchy (there's no tree structure here to carry that context implicitly).
        const QString name = toQString(match.node.name());
        const std::filesystem::path parentDir = match.node.path().parent_path();
        const std::filesystem::path relativeDir = std::filesystem::relative(parentDir, root);
        const QString context = (relativeDir.empty() || relativeDir == ".") ? QString() : toQString(relativeDir);
        headerNode->displayText = context.isEmpty() ? name : QStringLiteral("%1   —   %2").arg(name, context);

        for (const LineMatch& line : match.lines)
        {
            auto lineNode = std::make_unique<Node>();
            lineNode->parent = headerNode.get();
            lineNode->row = static_cast<int>(headerNode->children.size());
            lineNode->lineNumber = line.lineNumber;
            lineNode->lineTextUtf8 = line.textUtf8;
            headerNode->children.push_back(std::move(lineNode));
            ++m_matchCount;
        }

        m_root->children.push_back(std::move(headerNode));
    }

    endResetModel();
}
