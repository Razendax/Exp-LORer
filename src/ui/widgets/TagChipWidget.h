#pragma once

#include <QWidget>

#include "Tag.h"

class QLabel;
class QToolButton;

// One reusable tag chip: a colored QLabel name plus an optional trailing QToolButton, used by all
// three chip-bearing sections of TagPanelWidget (Architecture.md §14.9).
class TagChipWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Kind
    {
        ReadOnly, // §1.1 ancestor/folder tags — no button.
        Addable,  // §1.3 search results — trailing "+" button.
        Removable, // §1.4 selected-item tags — trailing "x" button.
    };

    TagChipWidget(Tag tag, Kind kind, QWidget* parent = nullptr);

    const Tag& tag() const noexcept { return m_tag; }

signals:
    void addClicked(Tag::Id tagId);
    void removeClicked(Tag::Id tagId);

private:
    Tag m_tag;
};
