#pragma once

#include <QLayout>
#include <QRect>
#include <QStyle>

// Standard Qt "Flow Layout" pattern: lays out child widgets left-to-right at their own
// sizeHint() width, wrapping to a new row when the current row runs out of horizontal space.
// Used by TagPanelWidget/TagManagerDialog (Architecture.md §14.9) to flow TagChipWidgets.
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget* parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout(int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override;
    QLayoutItem* takeAt(int index) override;

private:
    int doLayout(const QRect& rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem*> m_itemList;
    int m_hSpace = -1;
    int m_vSpace = -1;
};
