#pragma once

#include <optional>

#include <QWidget>

#include "Tag.h"

class QLabel;
class QToolButton;

// One reusable tag chip: a colored QLabel name plus an optional trailing QToolButton, used by all
// three chip-bearing sections of TagPanelWidget (Architecture.md §14.9), and, as Kind::Selectable,
// by TagManagerDialog (§14.17).
class TagChipWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Kind
    {
        ReadOnly, // §1.1 ancestor/folder tags — no button.
        Addable,  // §1.3 search results — trailing "+" button.
        Removable, // §1.4 selected-item tags — trailing "x" button.
        Selectable, // §14.17 Tag Manager — no +/x, whole chip is clickable/selectable.
    };

    // `count`, when set, adds a small secondary label showing it (Architecture.md §14.89's Tag
    // Manager file-count toggle) -- every other call site leaves it at std::nullopt and gets no
    // label, unchanged from before. `totalCount`, when also set (Architecture.md §14.91), is the
    // subtree total for a nested tag with children -- rendered as "count/totalCount" instead of a
    // bare count -- and is otherwise ignored.
    TagChipWidget(Tag tag, Kind kind, std::optional<int> count = std::nullopt, std::optional<int> totalCount = std::nullopt,
                  QWidget* parent = nullptr);

    const Tag& tag() const noexcept { return m_tag; }

    void setSelected(bool selected);
    bool isSelected() const noexcept { return m_selected; }

signals:
    void addClicked(Tag::Id tagId);
    void removeClicked(Tag::Id tagId);
    void clicked(Tag::Id tagId, Qt::KeyboardModifiers modifiers);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void updateChipStyle();

    Tag m_tag;
    Kind m_kind;
    QLabel* m_label = nullptr;
    QLabel* m_countLabel = nullptr;
    bool m_selected = false;
};
