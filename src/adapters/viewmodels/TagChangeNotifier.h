#pragma once

#include <QObject>

// Pure decoupling point (Architecture.md §14.x, Tags column): broadcasts that *some* tag
// assignment/definition changed somewhere, without either side needing to know about the other.
// TagListViewModel/TagManagerViewModel emit tagsChanged() after a successful mutation; every open
// tab's TabViewModel connects to it and re-fetches its own directory's tags. One shared instance,
// owned by CompositionRoot.
class TagChangeNotifier : public QObject
{
    Q_OBJECT

public:
    explicit TagChangeNotifier(QObject* parent = nullptr);

signals:
    void tagsChanged();
};
