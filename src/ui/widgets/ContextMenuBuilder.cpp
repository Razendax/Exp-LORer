#include "ContextMenuBuilder.h"

#include <QAction>
#include <QIcon>
#include <QImage>
#include <QMenu>
#include <QPixmap>
#include <QString>
#include <QVariant>

#include "IContextMenuProvider.h"

namespace
{
    QIcon iconFromEntry(const ContextMenuIcon& icon)
    {
        if (icon.rgba.empty() || icon.width <= 0 || icon.height <= 0)
        {
            return QIcon();
        }
        const QImage image(icon.rgba.data(), icon.width, icon.height, QImage::Format_RGBA8888);
        return QIcon(QPixmap::fromImage(image.copy()));
    }

    void appendEntries(QMenu* menu, const std::vector<ContextMenuEntry>& entries)
    {
        for (const ContextMenuEntry& entry : entries)
        {
            if (entry.isSeparator)
            {
                menu->addSeparator();
                continue;
            }

            const QString label = QString::fromStdString(entry.label);

            if (!entry.submenu.empty())
            {
                QMenu* submenu = menu->addMenu(label);
                submenu->setEnabled(entry.enabled);
                const QIcon icon = iconFromEntry(entry.icon);
                if (!icon.isNull())
                {
                    submenu->setIcon(icon);
                }
                appendEntries(submenu, entry.submenu);
                continue;
            }

            QAction* action = menu->addAction(label);
            action->setEnabled(entry.enabled);
            action->setData(QVariant::fromValue<uint>(entry.id));
            const QIcon icon = iconFromEntry(entry.icon);
            if (!icon.isNull())
            {
                action->setIcon(icon);
            }
        }
    }
}

QMenu* ContextMenuBuilder::buildItemMenu(const std::vector<ContextMenuEntry>& entries, const NativeActions& actions, QWidget* parent)
{
    auto* menu = new QMenu(parent);

    if (actions.open)
    {
        menu->addAction(actions.open);
        menu->addSeparator();
    }
    if (actions.extractHere)
    {
        menu->addAction(actions.extractHere);
    }
    if (actions.extractTo)
    {
        menu->addAction(actions.extractTo);
    }
    if (actions.extractHere || actions.extractTo)
    {
        menu->addSeparator();
    }
    if (actions.cut)
    {
        menu->addAction(actions.cut);
    }
    if (actions.copy)
    {
        menu->addAction(actions.copy);
    }
    if (actions.paste)
    {
        menu->addAction(actions.paste);
    }
    if (actions.cut || actions.copy || actions.paste)
    {
        menu->addSeparator();
    }

    appendEntries(menu, entries);
    if (!entries.empty())
    {
        menu->addSeparator();
    }

    if (actions.deleteAction)
    {
        menu->addAction(actions.deleteAction);
    }
    if (actions.rename)
    {
        menu->addAction(actions.rename);
    }
    if (actions.deleteAction || actions.rename)
    {
        menu->addSeparator();
    }

    if (actions.properties)
    {
        menu->addAction(actions.properties);
    }

    return menu;
}

QMenu* ContextMenuBuilder::buildBackgroundMenu(const std::vector<ContextMenuEntry>& entries, const NativeActions& actions,
                                                QWidget* parent)
{
    auto* menu = new QMenu(parent);

    if (actions.paste)
    {
        menu->addAction(actions.paste);
        menu->addSeparator();
    }
    if (actions.newFolder)
    {
        menu->addAction(actions.newFolder);
    }

    appendEntries(menu, entries);
    if (!entries.empty() || actions.newFolder)
    {
        menu->addSeparator();
    }

    if (actions.properties)
    {
        menu->addAction(actions.properties);
    }

    return menu;
}

std::optional<std::uint32_t> ContextMenuBuilder::entryIdForAction(const QAction* action)
{
    if (!action)
    {
        return std::nullopt;
    }

    const QVariant data = action->data();
    if (!data.isValid())
    {
        return std::nullopt;
    }

    bool ok = false;
    const uint value = data.toUInt(&ok);
    if (!ok)
    {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}
