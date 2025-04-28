#include "archive.h"

#include <bit7z/bitabstractarchivehandler.hpp>
#include <bit7z/bitarchivereader.hpp>
#include <bit7z/bitformat.hpp>
// #include <bit7z/bitnestedarchivereader.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

#ifdef _WIN32
static const std::vector<std::filesystem::path> libraryPaths{"dlls/7z.dll"};
#else
// list of 7z library paths including fallback locations
static const std::vector<std::filesystem::path> libraryPaths{
    "/usr/lib64/7z.so", "/usr/lib/7z.so", "lib/7z.so"};
#endif

using namespace bit7z;
using namespace std;

class FileDataImpl : public FileData
{
  friend class Archive;

public:
  FileDataImpl(std::filesystem::path fileName, uint64_t size, uint64_t crc,
               bool isDirectory)
      : m_FileName(std::move(fileName)), m_Size(size), m_CRC(crc),
        m_IsDirectory(isDirectory)
  {}

  [[nodiscard]] std::filesystem::path getArchiveFilePath() const override
  {
    return m_FileName;
  }

  [[nodiscard]] uint64_t getSize() const override { return m_Size; }

  void addOutputFilePath(std::filesystem::path const& fileName) override
  {
    m_OutputFilePaths.push_back(fileName);
  }

  [[nodiscard]] const std::vector<std::filesystem::path>&
  getOutputFilePaths() const override
  {
    return m_OutputFilePaths;
  }

  void clearOutputFilePaths() override { m_OutputFilePaths.clear(); }

  [[nodiscard]] bool isEmpty() const { return m_OutputFilePaths.empty(); }
  [[nodiscard]] bool isDirectory() const override { return m_IsDirectory; }
  [[nodiscard]] uint64_t getCRC() const override { return m_CRC; }

private:
  std::filesystem::path m_FileName;
  uint64_t m_Size;
  uint64_t m_CRC;
  std::vector<std::filesystem::path> m_OutputFilePaths;
  bool m_IsDirectory;
};

/// represents the connection to one archive and provides common functionality
class ArchiveImpl : public Archive
{

  // Callback that does nothing but avoid having to check if the callback is present
  // everytime.
  static LogCallback DefaultLogCallback;

public:
  ArchiveImpl();
  ~ArchiveImpl() override;

  [[nodiscard]] bool isValid() const override { return m_Valid; }

  [[nodiscard]] Error getLastError() const override { return m_LastError; }

  void setLogCallback(LogCallback logCallback) override
  {
    // Wrap the callback so that we do not have to check if it is set everywhere:
    m_LogCallback = logCallback ? logCallback : DefaultLogCallback;
  }

  bool open(std::filesystem::path const& archiveName,
            PasswordCallback passwordCallback) override;
  void close() override;

  [[nodiscard]] const std::vector<FileData*>& getFileList() const override
  {
    return m_FileList;
  }

  bool extract(std::filesystem::path const& outputDirectory,
               ProgressCallback progressCallback, FileChangeCallback fileChangeCallback,
               ErrorCallback errorCallback) override;

  void cancel() override;

private:
  void clearFileList();
  void resetFileList();
  void reportError(const native_string& message) const;

  // callback wrapper functions
  /** @returns true if we should continue extracting, false otherwise */
  [[nodiscard]] bool progressCallbackWrapper(uint64_t current) const;
  void fileChangeCallbackWrapper(const std::filesystem::path& path) const;
  native_string passwordCallbackWrapper();

  bool m_Valid;
  bool m_Nested;  // whether we got a nested archive, e.g. tar.gz; currently unused
  Error m_LastError;
  std::atomic<bool> m_shouldCancel = false;

  Bit7zLibrary* m_Library;
  BitArchiveReader* m_ArchivePtr;

  ProgressType m_ProgressType;
  uint64_t m_Total;
  FileChangeType m_FileChangeType;

  ProgressCallback m_ProgressCallback;
  FileChangeCallback m_FileChangeCallback;
  LogCallback m_LogCallback;
  ErrorCallback m_ErrorCallback;
  PasswordCallback m_PasswordCallback;

  std::vector<FileData*> m_FileList;

  native_string m_Password;
};

Archive::LogCallback ArchiveImpl::DefaultLogCallback([](LogLevel,
                                                        native_string const&) {});

ArchiveImpl::ArchiveImpl()
    : m_Valid(false), m_Nested(false), m_LastError(Error::ERROR_NONE),
      m_Library(nullptr), m_ArchivePtr(nullptr),
      m_ProgressType(ProgressType::EXTRACTION), m_Total(0),
      m_FileChangeType(FileChangeType::EXTRACTION_START)
{
  // Reset the log callback:
  ArchiveImpl::setLogCallback({});

  // the default constructor would look for "7z.dll" on windows and
  // "/usr/lib/p7zip/7z.so" on linux
  string error;
  for (const auto& path : libraryPaths) {
    try {
      m_Library = new Bit7zLibrary(path);
      m_Valid   = true;
      break;
    } catch (const BitException& ex) {
      error = ex.what();
    }
  }
  if (!m_Valid) {
    m_LogCallback(LogLevel::Error, format("Could not find 7z library: {}", error));
    m_LastError = Error::ERROR_LIBRARY_NOT_FOUND;
  }
}

ArchiveImpl::~ArchiveImpl()
{
  ArchiveImpl::close();
  delete m_Library;
}

bool ArchiveImpl::open(std::filesystem::path const& archiveName,
                       PasswordCallback passwordCallback)
{
  if (!m_Valid) {
    switch (m_LastError) {
    case Error::ERROR_LIBRARY_NOT_FOUND:
      m_LogCallback(LogLevel::Error, "Could not open 7z library");
      break;
    default:
      m_LogCallback(LogLevel::Error,
                    format("Unknown error, id: {}", static_cast<int>(m_LastError)));
    }
    return false;
  }

  // If it doesn't exist or is a directory, error
  if (!exists(archiveName) || is_directory(archiveName)) {
    m_LastError = Error::ERROR_ARCHIVE_NOT_FOUND;
    m_LogCallback(LogLevel::Error, "Archive not found");
    return false;
  }

  try {
    m_ArchivePtr = new BitArchiveReader(*m_Library, archiveName, BitFormat::Auto);

    m_PasswordCallback = passwordCallback;
    m_ArchivePtr->setPasswordCallback([this] {
      return passwordCallbackWrapper();
    });

    m_Total = m_ArchivePtr->size();

    m_LastError = Error::ERROR_NONE;
    resetFileList();
    return true;

  } catch (const BitException& ex) {
    m_LastError = Error::ERROR_FAILED_TO_OPEN_ARCHIVE;
    m_LogCallback(LogLevel::Error, ex.what());
    return false;
  }
}

void ArchiveImpl::close()
{
  delete m_ArchivePtr;
  m_ArchivePtr = nullptr;
  clearFileList();
  m_PasswordCallback = {};
  m_shouldCancel.store(false);
}

void ArchiveImpl::clearFileList()
{
  for (FileData* iter : m_FileList) {
    delete dynamic_cast<FileDataImpl*>(iter);
  }
  m_FileList.clear();
}

void ArchiveImpl::resetFileList()
{
  clearFileList();

  m_FileList.reserve(m_ArchivePtr->itemsCount());

  for (const auto& item : *m_ArchivePtr) {
    m_FileList.push_back(
        new FileDataImpl(item.path(), item.size(), item.crc(), item.isDir()));
  }

  // check if we got a nested archive
  // if (m_FileList.size() == 1) {
  //   if (m_FileList[0]->getArchiveFilePath().extension() == ".tar") {
  //     m_Nested = true;
  //   }
  // }
}

bool ArchiveImpl::extract(const std::filesystem::path& outputDirectory,
                          ProgressCallback progressCallback,
                          FileChangeCallback fileChangeCallback,
                          ErrorCallback errorCallback)
{
  if (!m_Valid) {
    return false;
  }

  try {
    m_ProgressCallback   = progressCallback;
    m_FileChangeCallback = fileChangeCallback;
    m_ErrorCallback      = errorCallback;

    // set file changed callback
    if (m_FileChangeCallback) {
      m_FileChangeType = FileChangeType::EXTRACTION_START;

      m_ArchivePtr->setFileCallback([this](const tstring& path) {
        fileChangeCallbackWrapper(std::forward<std::filesystem::path>(path));
      });
    }

    // we could test the archive if we wanted to by calling
    // m_ArchivePtr->test();

    // Retrieve the list of indices we want to extract:
    std::vector<uint32_t> indices;
    uint64_t totalSize = 0;

    for (std::size_t i = 0; i < m_FileList.size(); ++i) {
      auto* fileData = dynamic_cast<FileDataImpl*>(m_FileList[i]);
      if (!fileData->isEmpty()) {
        indices.push_back(i);
        totalSize += fileData->getSize();
      }
    }

    if (m_ProgressCallback) {
      m_ArchivePtr->setProgressCallback([this](uint64_t current) {
        return progressCallbackWrapper(current);
      });
    }

    std::error_code ec;

    std::filesystem::path tmpDir =
        std::filesystem::temp_directory_path(ec) / tmpnam(nullptr);
    if (ec || !create_directory(tmpDir, ec)) {
      m_LastError = Error::ERROR_OUT_OF_MEMORY;
      reportError(format("Error creating a temporary directory for extraction, {}",
                         ec.message()));
      return false;
    }

    // extract files into a temporary location
    m_ArchivePtr->extractTo(tmpDir, indices);

    // copy files to target locations
    for (const auto* fileData : m_FileList) {
      // handle directories
      if (fileData->isDirectory()) {
        for (const auto& outputFilePath : fileData->getOutputFilePaths()) {
          fs::path targetDirectory = outputDirectory / outputFilePath;
          create_directories(targetDirectory, ec);
          if (ec) {
            m_LastError = Error::ERROR_LIBRARY_ERROR;
            reportError(format("Error creating output directory %1: %2",
                               targetDirectory.native(), ec.message()));
            return false;
          }
        }
      } else {
        // handle files
        for (const auto& outputFilePath : fileData->getOutputFilePaths()) {
          // create output directory
          fs::path targetDirectory = outputDirectory;
          if (outputFilePath.has_parent_path()) {
            targetDirectory /= outputFilePath.parent_path();
          }
          create_directories(targetDirectory, ec);
          if (ec) {
            m_LastError = Error::ERROR_LIBRARY_ERROR;
            reportError(format("Error creating output directory {}: {}",
                               targetDirectory.native(), ec.message()));
            return false;
          }
          // copy file
          copy_file(tmpDir / fileData->getArchiveFilePath(),
                    outputDirectory / outputFilePath, ec);
          if (ec) {
            m_LastError = Error::ERROR_LIBRARY_ERROR;
            reportError(format("Error writing to output file {}: {}",
                               (outputDirectory / outputFilePath).native(),
                               ec.message()));
            return false;
          }
        }
      }
    }

    // example code to handle nested archives, this will probably be supported in
    // bit7z 4.1
    /*
    // handle nested archive
    if (m_Nested) {
      m_LogCallback(LogLevel::Info, "Extracting nested archive");
      BitNestedArchiveReader nestedReader{*m_Library, *m_ArchivePtr, BitFormat::Tar};

      // add callbacks
      // tar does not support encryption, so we don't need to bother with a password
      // callback
      nestedReader.setProgressCallback(m_ArchivePtr->progressCallback());
      nestedReader.setFileCallback(m_ArchivePtr->fileCallback());

      // NOTE: extracting a tar file can result in additional files like
      // pax_global_header that are not always shown in other software
      nestedReader.extractTo(outputDirectory);
    } else {
      m_ArchivePtr->extractTo(outputDirectory);
    }
  */

    return true;
  } catch (const BitException& ex) {
    if (m_shouldCancel) {
      m_LastError = Error::ERROR_EXTRACT_CANCELLED;
    } else {
      m_LastError = Error::ERROR_LIBRARY_ERROR;
    }
    reportError(ex.what());
    return false;
  }
}

void ArchiveImpl::cancel()
{
  m_shouldCancel.store(true);
}

void ArchiveImpl::reportError(const native_string& message) const
{
  if (m_ErrorCallback) {
    m_ErrorCallback(message);
  }
}

native_string ArchiveImpl::passwordCallbackWrapper()
{
  // only ask for password once
  if (m_Password.empty() && m_PasswordCallback) {
    m_Password = m_PasswordCallback();
  }
  return m_Password;
}

bool ArchiveImpl::progressCallbackWrapper(uint64_t current) const
{
  m_ProgressCallback(m_ProgressType, current, m_Total);
  return !m_shouldCancel;
}

void ArchiveImpl::fileChangeCallbackWrapper(const std::filesystem::path& path) const
{
  m_FileChangeCallback(m_FileChangeType, path);
}

DLLEXPORT std::unique_ptr<Archive> CreateArchive()
{
  return std::make_unique<ArchiveImpl>();
}