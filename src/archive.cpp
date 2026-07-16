#include "archive.h"

#include <bit7z/bit7zlibraryloader.hpp>
#include <bit7z/bitabstractarchivehandler.hpp>
#include <bit7z/bitarchivereader.hpp>
#include <bit7z/bitformat.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

using namespace bit7z;
using namespace std;
namespace fs = std::filesystem;

namespace
{

tstring getLibraryPath()
{
#ifdef __unix__
  // list of 7z library paths including fallback locations
  vector<fs::path> libraryPaths;

  if (getenv("APPIMAGE") != nullptr && getenv("APPDIR") != nullptr) {
    const fs::path appDir = getenv("APPDIR");
    libraryPaths          = {appDir / "lib/7z.so"};
  } else {
    libraryPaths.insert(libraryPaths.end(), {"lib/lib7zip.so", "/usr/lib/7zip/7z.so",
                                             "/usr/lib64/7zip/7z.so"});
  }

  for (const auto& libraryPath : libraryPaths) {
    if (std::filesystem::exists(libraryPath)) {
      return libraryPath;
    }
  }
  return "7z.so";
#else
  return to_tstring(L"dlls/7zip.dll");
#endif
}
}  // namespace

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
  void reportError(const tstring& message) const;

  // callback wrapper functions
  /** @returns true if we should continue extracting, false otherwise */
  [[nodiscard]] bool progressCallbackWrapper(uint64_t current) const;
  tstring passwordCallbackWrapper();

  bool m_Valid;
  Error m_LastError;
  std::atomic<bool> m_shouldCancel = false;

  Bit7zLibraryLoader m_Library;
  unique_ptr<BitArchiveReader> m_ArchivePtr;

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
    : m_Valid(false), m_LastError(Error::ERROR_NONE), m_ArchivePtr(nullptr),
      m_ProgressType(ProgressType::EXTRACTION), m_Total(0),
      m_FileChangeType(FileChangeType::EXTRACTION_START)
{
  // Reset the log callback:
  ArchiveImpl::setLogCallback({});

  error_code ec;
  m_Library.load(getLibraryPath(), ec);
  if (ec) {
    reportError(format(BIT7Z_STRING("Could not find 7z library: {}"),
                       to_tstring(ec.message())));
    m_LastError = Error::ERROR_LIBRARY_NOT_FOUND;
  } else {
    m_Valid = true;
  }
}

ArchiveImpl::~ArchiveImpl()
{
  ArchiveImpl::close();
}

bool ArchiveImpl::open(std::filesystem::path const& archiveName,
                       PasswordCallback passwordCallback)
{
  if (!m_Valid) {
    if (m_LastError == Error::ERROR_LIBRARY_NOT_FOUND) {
      reportError(BIT7Z_STRING("Could not open 7z library"));
    } else {
      reportError(
          format(BIT7Z_STRING("Unknown error, id: {}"), static_cast<int>(m_LastError)));
    }
    return false;
  }

  // If it doesn't exist or is a directory, error
  if (!exists(archiveName) || is_directory(archiveName)) {
    m_LastError = Error::ERROR_ARCHIVE_NOT_FOUND;
    reportError(format(BIT7Z_STRING("Archive file {} not found"),
                       to_tstring(archiveName.native())));
    return false;
  }

  try {
    if (BitArchiveReader::isHeaderEncrypted(m_Library, to_tstring(archiveName.native()),
                                            BitFormat::Auto)) {
      m_Password = passwordCallback();
    }

    m_ArchivePtr =
        make_unique<BitArchiveReader>(m_Library, to_tstring(archiveName.native()),
                                      BitFormat::Auto, to_tstring(m_Password));
    m_PasswordCallback = passwordCallback;
    m_ArchivePtr->setPasswordCallback([this] {
      return passwordCallbackWrapper();
    });

    m_LastError = Error::ERROR_NONE;
    resetFileList();
    return true;

  } catch (const BitException& ex) {
    m_LastError = Error::ERROR_FAILED_TO_OPEN_ARCHIVE;
    reportError(ex.what());
    return false;
  }
}

void ArchiveImpl::close()
{
  m_ArchivePtr.reset();
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
}

bool ArchiveImpl::extract(std::filesystem::path const& outputDirectory,
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

    // set file callback
    // currentFile is required to know which file is being extracted in the
    // RawDataCallback
    tstring currentFile;
    if (m_FileChangeCallback) {
      m_ArchivePtr->setFileCallback([&](const tstring& path) {
        currentFile = path;
        m_FileChangeCallback(m_FileChangeType, fs::path(path));
      });
    } else {
      m_ArchivePtr->setFileCallback([&](const tstring& path) {
        currentFile = path;
      });
    }

    // we could test the archive if we wanted to by calling
    // m_ArchivePtr->test();

    // Retrieve the list of indices we want to extract:
    vector<uint32_t> indices;

    m_Total = 0;

    std::unordered_map<tstring, vector<ofstream>> fileMap;

    error_code ec;
    create_directories(outputDirectory, ec);
    if (ec) {
      m_LastError = Error::ERROR_LIBRARY_ERROR;
      reportError(format(BIT7Z_STRING("Error creating output directory '{}': {}"),
                         to_tstring(outputDirectory.native()), ec.message()));
      return false;
    }

    for (size_t i = 0; i < m_FileList.size(); ++i) {
      auto* fileData = dynamic_cast<FileDataImpl*>(m_FileList[i]);

      vector<ofstream> outputs;
      const auto& outputFilePaths = fileData->getOutputFilePaths();
      outputs.reserve(outputFilePaths.size());

      for (const fs::path& outputFilePath : outputFilePaths) {
        if (fileData->isDirectory()) {
          fs::create_directories(outputFilePath, ec);
          if (ec) {
            m_LastError = Error::ERROR_LIBRARY_ERROR;
            reportError(format(BIT7Z_STRING("Error creating directory '{}': {}"),
                               to_tstring(outputFilePath.native()), ec.message()));
            return false;
          }
        } else {
          // create output directory
          if (outputFilePath.has_parent_path()) {
            fs::path parentPath = outputDirectory / outputFilePath.parent_path();
            fs::create_directories(parentPath, ec);
            if (ec) {
              m_LastError = Error::ERROR_LIBRARY_ERROR;
              reportError(format(BIT7Z_STRING("Error creating directory '{}': {}"),
                                 to_tstring(parentPath.native()), ec.message()));
              return false;
            }
          }
          ofstream ofs(outputDirectory / outputFilePath, ios::binary | ios::trunc);
          try {
            ofs.exceptions(ios::failbit | ios::badbit);
          } catch (const ios_base::failure& ex) {
            reportError(format(BIT7Z_STRING("Error opening '{}' for writing: {}"),
                               to_tstring((outputDirectory / outputFilePath).native()),
                               ex.what()));
            return false;
          }
          outputs.emplace_back(std::move(ofs));
        }
      }

      bool inserted = fileMap
                          .emplace(to_tstring(fileData->getArchiveFilePath().native()),
                                   std::move(outputs))
                          .second;
      if (!inserted) {
        reportError(format(BIT7Z_STRING("Error adding '{}' to file map"),
                           to_tstring(fileData->getArchiveFilePath().native())));
        m_LastError = Error::ERROR_LIBRARY_ERROR;
        return false;
      }
      if (!fileData->isEmpty()) {
        indices.push_back(static_cast<uint32_t>(i));
        m_Total += fileData->getSize();
      }
    }

    if (m_ProgressCallback) {
      m_ArchivePtr->setProgressCallback([this](const uint64_t current) {
        return progressCallbackWrapper(current);
      });
    }

    // extract files
    m_ArchivePtr->extractTo(
        [&](const byte_t* data, const std::size_t size) -> bool {
          try {
            for (auto& stream : fileMap.at(currentFile)) {
              stream.write(reinterpret_cast<const char*>(data),
                           static_cast<streamsize>(size));
            }
            return true;
          } catch (const std::out_of_range&) {
            reportError(
                format(BIT7Z_STRING("File {} not found in file map"), currentFile));
            return false;
          } catch (const ios_base::failure& ex) {
            reportError(format(BIT7Z_STRING("Error writing to {}: {}"), currentFile,
                               ex.what()));
            return false;
          }
        },
        indices);

    for (auto& fileData : m_FileList) {
      fileData->clearOutputFilePaths();
    }

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

void ArchiveImpl::reportError(const tstring& message) const
{
  if (m_ErrorCallback) {
    m_ErrorCallback(to_native_string(message));
  } else {
    m_LogCallback(LogLevel::Error, to_native_string(message));
  }
}

tstring ArchiveImpl::passwordCallbackWrapper()
{
  // only ask for password once
  if (m_Password.empty() && m_PasswordCallback) {
    m_Password = m_PasswordCallback();
  }
  return to_tstring(m_Password);
}

bool ArchiveImpl::progressCallbackWrapper(uint64_t current) const
{
  m_ProgressCallback(m_ProgressType, current, m_Total);
  return !m_shouldCancel.load();
}

DLLEXPORT std::unique_ptr<Archive> CreateArchive()
{
  return std::make_unique<ArchiveImpl>();
}
