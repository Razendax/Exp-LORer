#include <gtest/gtest.h>

#include <optional>

#include <QIcon>
#include <QObject>
#include <QString>

#include "FileDecorationRules.h"
#include "FileListModel.h"
#include "FileNode.h"

namespace
{
    namespace fs = std::filesystem;

    QString toQString(const fs::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    FileNode makeFile(const fs::path& path)
    {
        return FileNode::create(path, 0, std::chrono::system_clock::now(), std::chrono::system_clock::now(), FileType::Regular)
            .value();
    }

    // Records the row range from the model's dataChanged signal without needing the Qt Test
    // module's QSignalSpy (not a project dependency -- see vcpkg.json).
    struct DataChangedRecorder
    {
        int count = 0;
        int minRow = -1;
        int maxRow = -1;
        int lastColumn = -1;

        void onDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
        {
            ++count;
            minRow = topLeft.row();
            maxRow = bottomRight.row();
            lastColumn = topLeft.column();
            EXPECT_EQ(bottomRight.column(), lastColumn);
        }
    };

    class FileListModelTest : public ::testing::Test
    {
    protected:
        FileDecorationRules m_rules;
        QObject m_changeSource;
        FileListModel m_model{ m_rules, m_changeSource };
        DataChangedRecorder m_recorder;

        void SetUp() override
        {
            QObject::connect(&m_model, &FileListModel::dataChanged, &m_changeSource,
                              [this](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
                                  m_recorder.onDataChanged(topLeft, bottomRight);
                              });
        }
    };
}

// Architecture.md §14.x's "Fix slow folder-open for executable-heavy directories" follow-up: a
// single icon batch should only repaint the rows it actually resolved, not the whole NameColumn.
TEST_F(FileListModelTest, SetIconsScopesDataChangedToTheMatchedRow)
{
    const fs::path dir = fs::temp_directory_path() / "explorer_file_list_model_test";
    const FileNode a = makeFile(dir / "a.txt");
    const FileNode b = makeFile(dir / "b.txt");
    const FileNode c = makeFile(dir / "c.txt");
    m_model.setEntries(dir, { a, b, c });

    QHash<QString, QIcon> batch;
    batch.insert(toQString(b.path()), QIcon());
    m_model.setIcons(batch);

    ASSERT_EQ(m_recorder.count, 1);
    EXPECT_EQ(m_recorder.minRow, 1);
    EXPECT_EQ(m_recorder.maxRow, 1);
    EXPECT_EQ(m_recorder.lastColumn, FileListModel::NameColumn);
}

TEST_F(FileListModelTest, SetIconsSpansMinToMaxOfEveryMatchedRow)
{
    const fs::path dir = fs::temp_directory_path() / "explorer_file_list_model_test";
    const FileNode a = makeFile(dir / "a.txt");
    const FileNode b = makeFile(dir / "b.txt");
    const FileNode c = makeFile(dir / "c.txt");
    m_model.setEntries(dir, { a, b, c });

    QHash<QString, QIcon> batch;
    batch.insert(toQString(a.path()), QIcon());
    batch.insert(toQString(c.path()), QIcon());
    m_model.setIcons(batch);

    ASSERT_EQ(m_recorder.count, 1);
    EXPECT_EQ(m_recorder.minRow, 0);
    EXPECT_EQ(m_recorder.maxRow, 2);
}

TEST_F(FileListModelTest, SetIconsEmitsNothingWhenNoKeyIsCurrentlyVisible)
{
    const fs::path dir = fs::temp_directory_path() / "explorer_file_list_model_test";
    const FileNode a = makeFile(dir / "a.txt");
    m_model.setEntries(dir, { a });

    // A reply for a listing this model has since moved on from (or a hidden-filtered entry) --
    // either way, nothing visible to repaint.
    QHash<QString, QIcon> batch;
    batch.insert(toQString(dir / "stale.txt"), QIcon());
    m_model.setIcons(batch);

    EXPECT_EQ(m_recorder.count, 0);
}
