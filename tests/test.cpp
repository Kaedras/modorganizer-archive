#include "archive/archive.h"

#include <gtest/gtest.h>

using namespace std;
namespace fs = std::filesystem;

std::wstring passwordCallback()
{
  return L"password";
}

void logCallback(Archive::LogLevel, std::wstring const& log)
{
  std::wcout << log << '\n';
}

void errorCallback(std::wstring const& log)
{
  std::wcerr << log << '\n';
}

class ArchiveTest : public testing::TestWithParam<string>
{
  // You can implement all the usual fixture class members here.
  // To access the test parameter, call GetParam() from class
  // TestWithParam<T>.
};

TEST_P(ArchiveTest, Archive)
{
  const auto tmpDir = fs::temp_directory_path() / "archive-test";

  const string& archive = GetParam();

  auto a = CreateArchive();
  ASSERT_TRUE(a->isValid()) << "error " << static_cast<int>(a->getLastError());
  a->setLogCallback(logCallback);

  ASSERT_TRUE(a->open("files/" + archive, passwordCallback))
      << "error " << static_cast<int>(a->getLastError());

  vector<FileData*> files = a->getFileList();
  for (FileData* file : files) {
    file->addOutputFilePath(file->getArchiveFilePath());
  }

  EXPECT_TRUE(a->extract(tmpDir, nullptr, nullptr, errorCallback));
  // system("tree /tmp/archive-test");
  std::filesystem::remove_all(tmpDir);
}

INSTANTIATE_TEST_SUITE_P(Extract, ArchiveTest,
                         testing::Values("test.7z", "test_encrypted.7z",
                                         "test_encrypted_headers.7z", "test.zip",
                                         "test_encrypted.zip"));

// INSTANTIATE_TEST_SUITE_P(ExtractNested, ArchiveTest,
//                          testing::Values("test.tar.bz2",
//                                          "test.tar.zst"));
