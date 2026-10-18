#pragma once

#include <initializer_list>

#include <QSignalBlocker>
#include <QToolButton>

// Applies "exclusive but deselectable" behavior to a hand-rolled group of QToolButtons: when
// `button` becomes checked, every other button in `group` is unchecked (via QSignalBlocker, so
// doing so doesn't re-enter their own toggled handlers). This vendored Qt build's QButtonGroup has
// no ExclusionPolicy::ExclusiveOptional (only the classic always-one-selected exclusive mode), so
// callers that want "at most one of these checked, but any one of them togglable off again" wire
// each button's toggled signal to call this instead of using a QButtonGroup. Shared by
// SearchPanelWidget, SearchCriteriaPanel, and MediaViewerSearchPanelWidget's match-mode button
// rows (Whole Word / Exact Name / Regex).
inline void applyExclusiveToggle(QToolButton* button, bool checked, std::initializer_list<QToolButton*> group)
{
    if (!checked)
    {
        return;
    }

    for (QToolButton* other : group)
    {
        if (other != button && other->isChecked())
        {
            const QSignalBlocker blocker(other);
            other->setChecked(false);
        }
    }
}
