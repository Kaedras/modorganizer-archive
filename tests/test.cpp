#include "archive/archive.h"

#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace std;
namespace fs = std::filesystem;

QString passwordCallback()
{
  return QStringLiteral("password");
}

void logCallback(Archive::LogLevel, const QString& log)
{
  std::cout << log.toStdString() << '\n';
}

void errorCallback(const QString& log)
{
  std::cerr << log.toStdString() << '\n';
}

// create tmp dir and open an archive
#define INIT(filename)                                                                 \
  QTemporaryDir tmpDir;                                                                \
  ASSERT_TRUE(tmpDir.isValid()) << tmpDir.errorString().toStdString();                 \
  auto a = CreateArchive();                                                            \
  ASSERT_TRUE(a->isValid()) << a->errorString().toStdString();                         \
  a->setLogCallback(logCallback);                                                      \
  ASSERT_TRUE(a->open("files/"s + filename, passwordCallback))                         \
      << a->errorString().toStdString()

class ArchiveTest : public testing::TestWithParam<string>
{
  // You can implement all the usual fixture class members here.
  // To access the test parameter, call GetParam() from class
  // TestWithParam<T>.
};

TEST_P(ArchiveTest, Archive)
{
  const string& archive = GetParam();

  INIT(archive);

  for (FileData* file : a->getFileList()) {
    file->addOutputFilePath(file->getArchiveFilePath());
  }

  EXPECT_TRUE(a->extract(tmpDir.path().toStdString(), nullptr, nullptr, errorCallback))
      << a->errorString().toStdString();
  // system("tree /tmp/archive-test");
}

INSTANTIATE_TEST_SUITE_P(Extract, ArchiveTest,
                         testing::Values("test.7z", "test_encrypted.7z",
                                         "test_encrypted_headers.7z", "test.zip",
                                         "test_encrypted.zip"));

TEST(ArchiveTest, NoOutputPaths)
{
  INIT("test.7z");

  ASSERT_TRUE(a->extract(tmpDir.path().toStdString(), nullptr, nullptr, errorCallback))
      << a->errorString().toStdString();

  QDir dir(tmpDir.path());

  auto entryList = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
  ASSERT_TRUE(entryList.isEmpty())
      << "Directory contains " << entryList.join(';').toStdString();
}

// INSTANTIATE_TEST_SUITE_P(ExtractNested, ArchiveTest,
//                          testing::Values("test.tar.bz2",
//                                          "test.tar.zst"));
