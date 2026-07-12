#include "archive/archive.h"

#include <gtest/gtest.h>

using namespace std;
namespace fs = std::filesystem;

#ifdef __unix__
#define NATIVE_OUT cout
#define NATIVE_ERR cerr
#define NATIVE_STRING(str) str
#else
#define NATIVE_OUT wcout
#define NATIVE_ERR wcerr
#define NATIVE_STRING(str) L##str
#endif

struct TemporaryDir
{
  TemporaryDir() : path(fs::temp_directory_path() / "mo2-archive-test")
  {
    fs::create_directory(path, ec);
  }

  ~TemporaryDir() { fs::remove_all(path); }

  bool isValid() const { return !ec; }

  string errorString() const { return ec.message(); }

  const fs::path path;
  error_code ec;
};

native_string passwordCallback()
{
  return NATIVE_STRING("password");
}

void logCallback(Archive::LogLevel, const native_string& log)
{
  NATIVE_OUT << log << '\n';
}

void errorCallback(const native_string& log)
{
  NATIVE_ERR << log << '\n';
}

// create tmp dir and open an archive
#define INIT(filename)                                                                 \
  TemporaryDir tmpDir;                                                                 \
  ASSERT_TRUE(tmpDir.isValid()) << tmpDir.errorString();                               \
  auto a = CreateArchive();                                                            \
  ASSERT_TRUE(a->isValid()) << "error " << (int)a->getLastError();                     \
  a->setLogCallback(logCallback);                                                      \
  ASSERT_TRUE(a->open("files/"s + filename, passwordCallback))                         \
      << "error " << (int)a->getLastError()

class ArchiveTest : public testing::TestWithParam<string>
{};

TEST_P(ArchiveTest, Archive)
{
  INIT(GetParam());

  for (FileData* file : a->getFileList()) {
    file->addOutputFilePath(file->getArchiveFilePath());
  }

  EXPECT_TRUE(a->extract(tmpDir.path, nullptr, nullptr, errorCallback))
      << (int)a->getLastError();
}

INSTANTIATE_TEST_SUITE_P(Extract, ArchiveTest,
                         testing::Values("test.7z", "test_encrypted.7z",
                                         "test_encrypted_headers.7z", "test.rar",
                                         "test.zip", "test_encrypted.zip"));

// INSTANTIATE_TEST_SUITE_P(ExtractNested, ArchiveTest,
//                          testing::Values("test.tar.bz2",
//                                          "test.tar.zst"));

TEST(ArchiveTest, NoOutputPaths)
{
  INIT("test.7z");

  ASSERT_TRUE(a->extract(tmpDir.path, nullptr, nullptr, errorCallback))
      << (int)a->getLastError();

  // check if output directory is empty
  int count = 0;
  for ([[maybe_unused]] const auto& entry :
       fs::recursive_directory_iterator(tmpDir.path)) {
    ++count;
  }
  ASSERT_EQ(count, 0) << "output directory is not empty";
}

TEST(ArchiveTest, FileChangeCallback)
{
  INIT("test.7z");

  for (FileData* file : a->getFileList()) {
    if (file->getArchiveFilePath().string().starts_with("test")) {
      file->addOutputFilePath(file->getArchiveFilePath());
    }
  }

  string callbackFiles;

  Archive::FileChangeCallback fileChangeCallback =
      [&](Archive::FileChangeType, std::filesystem::path const& path) {
        callbackFiles += path.generic_string() + ";";
      };

  ASSERT_TRUE(a->extract(tmpDir.path, nullptr, fileChangeCallback, errorCallback))
      << (int)a->getLastError();

  // remove trailing ';'
  if (!callbackFiles.empty()) {
    callbackFiles.pop_back();
  }

  const string expectedResult = "test/b.txt";

  EXPECT_EQ(callbackFiles, expectedResult);

  int count = 0;
  for ([[maybe_unused]] const auto& entry :
       fs::recursive_directory_iterator(tmpDir.path)) {
    if (entry.is_regular_file()) {
      ++count;
    }
  }
  ASSERT_EQ(count, 1);
}
