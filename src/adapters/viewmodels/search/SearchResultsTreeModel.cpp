#include "SearchResultsTreeModel.h"

#include <algorithm>

#include <QFileInfo>

#include "PathContainment.h"
#include "PathUtf8.h"
#include "UnicodeCaseFold.h"

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    // Non-empty components only, matching FolderTreeModel.cpp's own helper of the same shape.
    std::vector<std::filesystem::path> componentsOf(const std::filesystem::path& path)
    {
        std::vector<std::filesystem::path> components;
        for (const auto& component : path)
        {
            if (!component.empty())
            {
                components.push_back(component);
            }
        }
        return components;
    }

    // Case-insensitive name order (Windows semantics), folders before files -- mirrors
    // FileNavigationUseCase::sortBy's SortCriterion::Name grouping. Takes plain fields rather than
    // a Node* since Node is a private nested type this anonymous namespace has no access to.
    struct SortKey
    {
        bool isFile;
        std::string folded;
        std::string exact;

        SortKey(bool isDirectory, const std::filesystem::path& name)
            : isFile(!isDirectory)
            , folded(UnicodeCaseFold::foldUtf8(PathUtf8::toUtf8(name)))
            , exact(PathUtf8::toUtf8(name))
        {
        }

        bool operator<(const SortKey& other) const
        {
            if (isFile != other.isFile)
            {
                return !isFile;
            }
            return folded != other.folded ? folded < other.folded : exact < other.exact;
        }
    };
}

struct SearchResultsTreeModel::Node
{
    std::filesystem::path path;
    QString displayName;
    Node* parent = nullptr;
    int row = 0;
    std::vector<std::unique_ptr<Node>> children;
    bool isMatch = false;
    bool isDirectory = true; // every ancestor segment is a folder; a match node overrides this

    void renumberFrom(std::size_t first)
    {
        for (std::size_t i = first; i < children.size(); ++i)
        {
            children[i]->row = static_cast<int>(i);
        }
    }
};

SearchResultsTreeModel::SearchResultsTreeModel(QObject* parent)
    : QAbstractItemModel(parent)
    , m_root(std::make_unique<Node>())
{
}

SearchResultsTreeModel::~SearchResultsTreeModel() = default;

SearchResultsTreeModel::Node* SearchResultsTreeModel::nodeFor(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<Node*>(index.internalPointer()) : m_root.get();
}

QModelIndex SearchResultsTreeModel::indexFor(const Node* node) const
{
    if (node == nullptr || node == m_root.get())
    {
        return QModelIndex();
    }
    return createIndex(node->row, 0, const_cast<Node*>(node));
}

QModelIndex SearchResultsTreeModel::index(int row, int column, const QModelIndex& parent) const
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

QModelIndex SearchResultsTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
    {
        return QModelIndex();
    }
    return indexFor(nodeFor(child)->parent);
}

int SearchResultsTreeModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0)
    {
        return 0;
    }
    return static_cast<int>(nodeFor(parent)->children.size());
}

int SearchResultsTreeModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant SearchResultsTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    const Node* node = nodeFor(index);
    switch (role)
    {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return node->displayName;
        case Qt::ToolTipRole:
            return toQString(node->path);
        case PathRole:
            return toQString(node->path);
        case IsDirectoryRole:
            return node->isDirectory;
        case Qt::DecorationRole:
            return node->isDirectory ? m_iconProvider.icon(QFileIconProvider::Folder)
                                      : m_iconProvider.icon(QFileInfo(toQString(node->path)));
        default:
            return QVariant();
    }
}

Qt::ItemFlags SearchResultsTreeModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void SearchResultsTreeModel::setRoot(const std::filesystem::path& root)
{
    m_searchRoot = root;
}

void SearchResultsTreeModel::setResults(const std::vector<FileNode>& matches)
{
    beginResetModel();

    m_root = std::make_unique<Node>();

    const std::vector<std::filesystem::path> rootComponents = componentsOf(m_searchRoot);

    for (const FileNode& match : matches)
    {
        const std::vector<std::filesystem::path> matchComponents = componentsOf(match.path());
        if (matchComponents.size() <= rootComponents.size())
        {
            // Not actually under the search root -- shouldn't happen, skip defensively.
            continue;
        }

        Node* current = m_root.get();
        std::filesystem::path currentPath = m_searchRoot;

        for (std::size_t i = rootComponents.size(); i < matchComponents.size(); ++i)
        {
            currentPath /= matchComponents[i];

            const auto found =
                std::find_if(current->children.begin(), current->children.end(),
                              [&currentPath](const std::unique_ptr<Node>& child) { return pathsEqualCaseInsensitive(child->path, currentPath); });

            Node* child;
            if (found != current->children.end())
            {
                child = found->get();
            }
            else
            {
                auto node = std::make_unique<Node>();
                node->path = currentPath;
                node->displayName = toQString(matchComponents[i]);
                node->parent = current;
                current->children.push_back(std::move(node));
                child = current->children.back().get();
            }

            current = child;

            const bool isLastSegment = (i == matchComponents.size() - 1);
            if (isLastSegment)
            {
                current->isMatch = true;
                current->isDirectory = match.isDirectory();
                current->path = match.path();
            }
        }
    }

    sortChildren(m_root.get());

    endResetModel();
}

void SearchResultsTreeModel::sortChildren(Node* node)
{
    std::sort(node->children.begin(), node->children.end(), [](const std::unique_ptr<Node>& a, const std::unique_ptr<Node>& b) {
        return SortKey(a->isDirectory, a->path.filename()) < SortKey(b->isDirectory, b->path.filename());
    });
    node->renumberFrom(0);

    for (auto& child : node->children)
    {
        sortChildren(child.get());
    }
}

std::filesystem::path SearchResultsTreeModel::pathForIndex(const QModelIndex& index) const
{
    return index.isValid() ? nodeFor(index)->path : std::filesystem::path();
}

bool SearchResultsTreeModel::isDirectoryForIndex(const QModelIndex& index) const
{
    return index.isValid() && nodeFor(index)->isDirectory;
}
