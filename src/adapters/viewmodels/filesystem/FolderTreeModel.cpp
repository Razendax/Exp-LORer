#include "FolderTreeModel.h"

#include <algorithm>
#include <string>

#include <QFileInfo>

#include "ArchiveExtensions.h"
#include "FileNavigationUseCase.h"
#include "PathContainment.h"
#include "PathUtf8.h"
#include "TabViewModel.h"
#include "UnicodeCaseFold.h"
#include "VirtualPaths.h"

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    // Non-empty components only, so a trailing separator ("C:\a\") doesn't add a phantom level.
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

    // Case-insensitive name order (Windows semantics), ties broken by the exact UTF-8 text so the
    // order is total and stable. Never narrows through path::string() (Architecture.md §6).
    struct SortKey
    {
        std::string folded;
        std::string exact;

        explicit SortKey(const std::filesystem::path& name)
            : folded(UnicodeCaseFold::foldUtf8(PathUtf8::toUtf8(name)))
            , exact(PathUtf8::toUtf8(name))
        {
        }

        bool operator<(const SortKey& other) const
        {
            return folded != other.folded ? folded < other.folded : exact < other.exact;
        }
    };
}

struct FolderTreeModel::Node
{
    std::filesystem::path path;
    QString displayName;
    Node* parent = nullptr;
    int row = 0; // cached position in parent->children, kept current by renumberFrom()
    std::vector<std::unique_ptr<Node>> children;
    bool childrenLoaded = false;

    bool isThisPc() const { return parent != nullptr && parent->parent == nullptr; }
    bool isTopLevel() const { return isThisPc() || (parent != nullptr && parent->isThisPc()); }

    void renumberFrom(std::size_t first)
    {
        for (std::size_t i = first; i < children.size(); ++i)
        {
            children[i]->row = static_cast<int>(i);
        }
    }

    void rebase(const std::filesystem::path& oldRoot, const std::filesystem::path& newRoot)
    {
        path = rebasePath(path, oldRoot, newRoot);
        for (auto& child : children)
        {
            child->rebase(oldRoot, newRoot);
        }
    }
};

FolderTreeModel::FolderTreeModel(FileNavigationUseCase& fileNavigationUseCase, QObject* parent)
    : QAbstractItemModel(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_root(std::make_unique<Node>())
    , m_showHiddenFiles(FileListModel::showHiddenFilesEnabled())
{
    // "This PC" exists up front (no I/O); its drives/quick-access folders are listed on first
    // expand or reveal, like every other level.
    auto thisPc = std::make_unique<Node>();
    thisPc->path = VirtualPaths::ThisPC;
    thisPc->displayName = tr("This PC");
    thisPc->parent = m_root.get();
    m_root->children.push_back(std::move(thisPc));
    m_root->childrenLoaded = true;
}

FolderTreeModel::~FolderTreeModel() = default;

FolderTreeModel::Node* FolderTreeModel::nodeFor(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<Node*>(index.internalPointer()) : m_root.get();
}

QModelIndex FolderTreeModel::indexFor(const Node* node) const
{
    if (node == nullptr || node == m_root.get())
    {
        return QModelIndex();
    }
    return createIndex(node->row, 0, const_cast<Node*>(node));
}

QModelIndex FolderTreeModel::index(int row, int column, const QModelIndex& parent) const
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

QModelIndex FolderTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
    {
        return QModelIndex();
    }
    return indexFor(nodeFor(child)->parent);
}

int FolderTreeModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0)
    {
        return 0;
    }
    return static_cast<int>(nodeFor(parent)->children.size());
}

int FolderTreeModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant FolderTreeModel::data(const QModelIndex& index, int role) const
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
            return node->isThisPc() ? QVariant() : QVariant(toQString(node->path));
        case PathRole:
            return toQString(node->path);
        case IsDirectoryRole:
            return true;
        case Qt::DecorationRole:
        {
            // Same QFileIconProvider precedent as FileListModel's DecorationRole. Only This PC's own
            // entries get a per-path shell icon (drive / special folder icons); deeper levels share
            // the generic folder icon so expanding a large folder costs no per-row shell lookup.
            const QFileIconProvider& provider = m_iconProvider;
            if (node->isThisPc())
            {
                return provider.icon(QFileIconProvider::Computer);
            }
            if (node->parent->isThisPc())
            {
                return node->path.has_filename() ? provider.icon(QFileInfo(toQString(node->path)))
                                                  : provider.icon(QFileIconProvider::Drive);
            }
            return provider.icon(QFileIconProvider::Folder);
        }
        default:
            return QVariant();
    }
}

Qt::ItemFlags FolderTreeModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }

    Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (!nodeFor(index)->isTopLevel())
    {
        result |= Qt::ItemIsEditable;
    }
    return result;
}

bool FolderTreeModel::hasChildren(const QModelIndex& parent) const
{
    const Node* node = nodeFor(parent);
    return !node->childrenLoaded || !node->children.empty();
}

bool FolderTreeModel::canFetchMore(const QModelIndex& parent) const
{
    return parent.isValid() && !nodeFor(parent)->childrenLoaded;
}

void FolderTreeModel::fetchMore(const QModelIndex& parent)
{
    if (parent.isValid())
    {
        loadChildren(nodeFor(parent));
    }
}

void FolderTreeModel::loadChildren(Node* node)
{
    if (node->childrenLoaded)
    {
        return;
    }

    // Synchronous on the UI thread, like every other navigation listing (Architecture.md §5's
    // accepted gap) -- one level per expand keeps each call small.
    auto result = m_fileNavigationUseCase.listDirectory(node->path);
    if (result.hasError())
    {
        // Access denied / vanished: treat as a leaf rather than surfacing an error.
        node->childrenLoaded = true;
        const QModelIndex index = indexFor(node);
        emit dataChanged(index, index);
        return;
    }

    applyEntries(node, result.value());
}

void FolderTreeModel::applyEntries(Node* node, const std::vector<FileNode>& entries)
{
    // Desired children, in display order. This PC keeps the repository's own order (quick-access
    // folders, then drives); every real folder level is sorted by name.
    struct Desired
    {
        std::filesystem::path path;
        QString displayName;
    };
    std::vector<Desired> desired;
    for (const FileNode& entry : entries)
    {
        if (!entry.isDirectory())
        {
            continue;
        }
        // A hidden folder on the way to the last revealed path stays visible even with hidden
        // files off -- otherwise browsing anywhere under e.g. AppData would leave the tree stuck
        // at the user profile folder.
        if (entry.isHidden() && !m_showHiddenFiles &&
            (m_revealedPath.empty() || !isPathWithinOrEqual(m_revealedPath, node->path / entry.name())))
        {
            continue;
        }

        if (node->isThisPc())
        {
            const QString name = entry.displayName() ? QString::fromStdString(*entry.displayName()) : toQString(entry.name());
            desired.push_back({ entry.path(), name });
        }
        else
        {
            // Built from the node's own path rather than entry.path(): a tab may have navigated
            // here with different letter case than the tree uses, and the tree's paths must stay
            // consistent with each other.
            desired.push_back({ node->path / entry.name(), toQString(entry.name()) });
        }
    }
    if (!node->isThisPc())
    {
        std::vector<std::pair<SortKey, Desired>> keyed;
        keyed.reserve(desired.size());
        for (Desired& item : desired)
        {
            keyed.emplace_back(SortKey(item.path.filename()), std::move(item));
        }
        std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        desired.clear();
        for (auto& [key, item] : keyed)
        {
            desired.push_back(std::move(item));
        }
    }

    const bool wasLoaded = node->childrenLoaded;
    const bool wasEmpty = node->children.empty();
    node->childrenLoaded = true;
    const QModelIndex parentIndex = indexFor(node);

    const auto makeNode = [node](const Desired& item) {
        auto child = std::make_unique<Node>();
        child->path = item.path;
        child->displayName = item.displayName;
        child->parent = node;
        return child;
    };

    if (wasEmpty)
    {
        // First population (the common fetchMore path): one bulk insert.
        if (!desired.empty())
        {
            beginInsertRows(parentIndex, 0, static_cast<int>(desired.size()) - 1);
            for (const Desired& item : desired)
            {
                node->children.push_back(makeNode(item));
            }
            node->renumberFrom(0);
            endInsertRows();
        }
        else if (!wasLoaded)
        {
            // No subfolders after all -- let the view drop the optimistic expand arrow.
            emit dataChanged(parentIndex, parentIndex);
        }
        return;
    }

    // Diff against the existing children so survivors keep their own loaded subtrees.
    const auto isDesired = [&desired](const std::filesystem::path& path) {
        return std::any_of(desired.begin(), desired.end(), [&path](const Desired& item) { return item.path == path; });
    };
    for (int row = static_cast<int>(node->children.size()) - 1; row >= 0; --row)
    {
        if (!isDesired(node->children[static_cast<std::size_t>(row)]->path))
        {
            beginRemoveRows(parentIndex, row, row);
            node->children.erase(node->children.begin() + row);
            node->renumberFrom(static_cast<std::size_t>(row));
            endRemoveRows();
        }
    }

    for (std::size_t i = 0; i < desired.size(); ++i)
    {
        const Desired& item = desired[i];
        if (i < node->children.size() && node->children[i]->path == item.path)
        {
            Node* existing = node->children[i].get();
            if (existing->displayName != item.displayName)
            {
                existing->displayName = item.displayName;
                const QModelIndex index = indexFor(existing);
                emit dataChanged(index, index);
            }
            continue;
        }

        const auto found = std::find_if(node->children.begin() + static_cast<std::ptrdiff_t>(i), node->children.end(),
                                        [&item](const std::unique_ptr<Node>& child) { return child->path == item.path; });
        if (found != node->children.end())
        {
            const int from = static_cast<int>(found - node->children.begin());
            beginMoveRows(parentIndex, from, from, parentIndex, static_cast<int>(i));
            std::rotate(node->children.begin() + static_cast<std::ptrdiff_t>(i), found, found + 1);
            node->renumberFrom(i);
            endMoveRows();
            continue;
        }

        beginInsertRows(parentIndex, static_cast<int>(i), static_cast<int>(i));
        node->children.insert(node->children.begin() + static_cast<std::ptrdiff_t>(i), makeNode(item));
        node->renumberFrom(i);
        endInsertRows();
    }

    if (node->children.empty())
    {
        emit dataChanged(parentIndex, parentIndex);
    }
}

std::vector<FolderTreeModel::Node*> FolderTreeModel::nodesFor(const std::filesystem::path& path) const
{
    std::vector<Node*> found;
    if (path.empty())
    {
        return found;
    }

    Node* thisPc = m_root->children.front().get();
    if (path == VirtualPaths::ThisPC)
    {
        found.push_back(thisPc);
        return found;
    }

    // Depth-first, only descending into subtrees that can contain `path` -- a folder can appear
    // under both its drive and a quick-access entry, so every match is collected.
    std::vector<Node*> stack;
    for (auto& child : thisPc->children)
    {
        stack.push_back(child.get());
    }
    while (!stack.empty())
    {
        Node* node = stack.back();
        stack.pop_back();
        if (!isPathWithinOrEqual(path, node->path))
        {
            continue;
        }
        if (pathsEqualCaseInsensitive(path, node->path))
        {
            found.push_back(node);
            continue;
        }
        for (auto& child : node->children)
        {
            stack.push_back(child.get());
        }
    }
    return found;
}

std::vector<FolderTreeModel::Node*> FolderTreeModel::loadedNodesFor(const std::filesystem::path& path) const
{
    std::vector<Node*> nodes = nodesFor(path);
    std::erase_if(nodes, [](const Node* node) { return !node->childrenLoaded; });
    return nodes;
}

void FolderTreeModel::collectLoaded(Node* node, std::vector<Node*>& out) const
{
    if (!node->childrenLoaded)
    {
        return;
    }
    if (node != m_root.get())
    {
        out.push_back(node);
    }
    for (auto& child : node->children)
    {
        collectLoaded(child.get(), out);
    }
}

QModelIndex FolderTreeModel::indexForPath(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return QModelIndex();
    }

    Node* thisPc = m_root->children.front().get();
    if (path == VirtualPaths::ThisPC)
    {
        return indexFor(thisPc);
    }

    // Archives are files and never appear in the tree: reveal the folder containing the
    // outermost archive instead (Architecture.md §14.28/§14.41).
    std::filesystem::path target = path;
    if (auto archive = ArchiveExtensions::archiveAncestorInPath(path))
    {
        target = archive->parent_path();
    }
    const std::vector<std::filesystem::path> targetComponents = componentsOf(target);
    m_revealedPath = target;

    loadChildren(thisPc);

    // Most specific This PC entry wins: a Downloads subfolder is revealed under "Downloads", not
    // under its drive (same as Explorer's navigation pane).
    Node* current = nullptr;
    std::size_t currentDepth = 0;
    for (auto& child : thisPc->children)
    {
        if (!isPathWithinOrEqual(target, child->path))
        {
            continue;
        }
        const std::size_t depth = componentsOf(child->path).size();
        if (current == nullptr || depth > currentDepth)
        {
            current = child.get();
            currentDepth = depth;
        }
    }
    if (current == nullptr)
    {
        return QModelIndex();
    }

    while (currentDepth < targetComponents.size())
    {
        loadChildren(current);

        const std::filesystem::path& nextName = targetComponents[currentDepth];
        const auto findNext = [current, &nextName]() {
            return std::find_if(current->children.begin(), current->children.end(), [&nextName](const auto& child) {
                return pathsEqualCaseInsensitive(child->path.filename(), nextName);
            });
        };
        auto next = findNext();
        if (next == current->children.end() && !m_showHiddenFiles)
        {
            // Possibly a hidden folder filtered out by an earlier listing, before it was on the
            // revealed path: re-list once now that m_revealedPath pins it.
            auto result = m_fileNavigationUseCase.listDirectory(current->path);
            if (result.hasValue())
            {
                applyEntries(current, result.value());
            }
            next = findNext();
        }
        if (next == current->children.end())
        {
            // Vanished or not a directory: stop at the deepest reachable level.
            break;
        }
        current = next->get();
        ++currentDepth;
    }

    return indexFor(current);
}

std::filesystem::path FolderTreeModel::pathForIndex(const QModelIndex& index) const
{
    return index.isValid() ? nodeFor(index)->path : std::filesystem::path();
}

bool FolderTreeModel::isTopLevelLocation(const QModelIndex& index) const
{
    return index.isValid() && nodeFor(index)->isTopLevel();
}

void FolderTreeModel::setActiveTab(TabViewModel* tab)
{
    disconnect(m_pathConnection);
    disconnect(m_listingConnection);

    if (tab != nullptr)
    {
        m_pathConnection = connect(tab, &TabViewModel::currentPathChanged, this, [this](const std::filesystem::path& path) {
            m_activePath = path;
            emit activePathChanged(path);
        });
        // Piggybacks on the tab's own listings (navigation, refresh, live-watch updates), so the
        // browsed folder's subfolders stay current in the tree at zero extra I/O.
        m_listingConnection = connect(tab, &TabViewModel::directoryContentsChanged, this, &FolderTreeModel::applyListing);
        m_activePath = tab->currentPath();
    }
    else
    {
        m_activePath.clear();
    }

    emit activePathChanged(m_activePath);
}

void FolderTreeModel::refreshDirectory(const std::filesystem::path& directory)
{
    for (Node* node : loadedNodesFor(directory))
    {
        auto result = m_fileNavigationUseCase.listDirectory(node->path);
        if (result.hasValue())
        {
            applyEntries(node, result.value());
        }
    }
}

void FolderTreeModel::applyListing(const std::filesystem::path& directory, const std::vector<FileNode>& entries)
{
    // Unlike refreshDirectory(), a not-yet-listed node is populated too: the listing is free.
    for (Node* node : nodesFor(directory))
    {
        applyEntries(node, entries);
    }
}

void FolderTreeModel::renameNode(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    for (Node* node : nodesFor(source))
    {
        if (node->isTopLevel())
        {
            continue;
        }

        Node* parentNode = node->parent;
        const QModelIndex parentIndex = indexFor(parentNode);

        if (!pathsEqualCaseInsensitive(source.parent_path(), destination.parent_path()))
        {
            // A move rather than a same-folder rename: drop the node here; the destination
            // folder's own refresh picks it up.
            const int row = node->row;
            beginRemoveRows(parentIndex, row, row);
            parentNode->children.erase(parentNode->children.begin() + row);
            parentNode->renumberFrom(static_cast<std::size_t>(row));
            endRemoveRows();
            continue;
        }

        const std::filesystem::path oldPath = node->path;
        const std::filesystem::path newPath = parentNode->path / destination.filename();
        node->rebase(oldPath, newPath);
        node->displayName = toQString(newPath.filename());
        const QModelIndex index = indexFor(node);
        emit dataChanged(index, index);

        // Re-sort among siblings.
        const SortKey key(newPath.filename());
        int target = 0;
        for (const auto& sibling : parentNode->children)
        {
            if (sibling.get() != node && SortKey(sibling->path.filename()) < key)
            {
                ++target;
            }
        }
        const int from = node->row;
        if (target == from)
        {
            continue;
        }

        // beginMoveRows' destination is an index into the pre-move list.
        beginMoveRows(parentIndex, from, from, parentIndex, target > from ? target + 1 : target);
        auto moved = std::move(parentNode->children[static_cast<std::size_t>(from)]);
        parentNode->children.erase(parentNode->children.begin() + from);
        parentNode->children.insert(parentNode->children.begin() + target, std::move(moved));
        parentNode->renumberFrom(static_cast<std::size_t>(std::min(from, target)));
        endMoveRows();
    }
}

void FolderTreeModel::setShowHiddenFiles(bool show)
{
    if (show == m_showHiddenFiles)
    {
        return;
    }
    m_showHiddenFiles = show;

    // Collected as paths, not Node*: re-listing a parent can destroy descendants collected after it.
    std::vector<Node*> loaded;
    collectLoaded(m_root.get(), loaded);
    std::vector<std::filesystem::path> paths;
    paths.reserve(loaded.size());
    for (const Node* node : loaded)
    {
        paths.push_back(node->path);
    }

    for (const std::filesystem::path& path : paths)
    {
        refreshDirectory(path);
    }
}
