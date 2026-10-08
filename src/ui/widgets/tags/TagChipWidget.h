#pragma once

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

    TagChipWidget(Tag tag, Kind kind, QWidget* parent = nullptr);

    const Tag& tag() const noexcept { return m_tag; }

    void setSelected(bool selected);
    bool isSelected() const noexcept { return m_selected; }

signals:
    void addClicked(Tag::Id tagId);
    void removeClicked(Tag::Id tagId);
    void clicked(Tag::Id tagId);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void updateChipStyle();

    Tag m_tag;
    Kind m_kind;
    QLabel* m_label = nullptr;
    bool m_selected = false;
};
