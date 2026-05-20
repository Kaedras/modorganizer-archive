#include "archive.h"

#include <bit7z/bitabstractarchivehandler.hpp>
#include <bit7z/bitarchivereader.hpp>
#include <bit7z/bitformat.hpp>

#include <QTemporaryDir>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

using namespace bit7z;
using namespace std;
using namespace Qt::StringLiterals;

namespace
{
tstring toNative(const QString& s)
{
#ifdef __unix__
  return s.toStdString();
#else
  return to_tstring(s.toStdWString());
#endif
}

tstring getLibraryPath()
{
#ifdef __unix__
  // list of 7z library paths including fallback locations
  static constexpr std::array libraryPaths{"lib/lib7zip.so", "/usr/lib/7zip/7z.so",
                                           "/usr/lib64/7zip/7z.so"};
  for (const auto& libraryPath : libraryPaths) {
    if (std::filesystem::exists(libraryPath)) {
      return libraryPath;
    }
  }
  return "7z.so";
#else
  return to_tstring(L"dlls/7z.dll");
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

  virtual std::filesystem::path getArchiveFilePath() const override
  {
    return m_FileName;
  }
  virtual uint64_t getSize() const override { return m_Size; }

  virtual void addOutputFilePath(std::filesystem::path const& fileName) override
  {
    m_OutputFilePaths.push_back(fileName);
  }
  virtual const std::vector<std::filesystem::path>& getOutputFilePaths() const override
  {
    return m_OutputFilePaths;
  }

  virtual void clearOutputFilePaths() override { m_OutputFilePaths.clear(); }

  bool isEmpty() const { return m_OutputFilePaths.empty(); }
  virtual bool isDirectory() const override { return m_IsDirectory; }
  virtual uint64_t getCRC() const override { return m_CRC; }

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

  bool isValid() const override { return m_Valid; }

  Error getLastError() const override { return m_LastError; }
  QString errorString() const override;
  virtual void setLogCallback(LogCallback logCallback) override
  {
    // Wrap the callback so that we do not have to check if it is set everywhere:
    m_LogCallback = logCallback ? logCallback : DefaultLogCallback;
  }

  virtual bool open(std::filesystem::path const& archiveName,
                    PasswordCallback passwordCallback) override;
  virtual void close() override;
  const std::vector<FileData*>& getFileList() const override { return m_FileList; }
  virtual bool extract(std::filesystem::path const& outputDirectory,
                       ProgressCallback progressCallback,
                       FileChangeCallback fileChangeCallback,
                       ErrorCallback errorCallback) override;

  virtual void cancel() override;

private:
  void clearFileList();
  void resetFileList();
  void reportError(const QString& message) const;

  // callback wrapper functions
  /** @returns true if we should continue extracting, false otherwise */
  [[nodiscard]] bool progressCallbackWrapper(uint64_t current) const;
  void fileChangeCallbackWrapper(const std::filesystem::path& path) const;
  tstring passwordCallbackWrapper();

  bool m_Valid;
  Error m_LastError;
  std::atomic<bool> m_shouldCancel = false;

  unique_ptr<Bit7zLibrary> m_Library;
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

  QString m_Password;
};

Archive::LogCallback ArchiveImpl::DefaultLogCallback([](LogLevel, QString const&) {});

ArchiveImpl::ArchiveImpl()
    : m_Valid(false), m_LastError(Error::ERROR_NONE), m_Library(nullptr),
      m_ArchivePtr(nullptr), m_ProgressType(ProgressType::EXTRACTION), m_Total(0),
      m_FileChangeType(FileChangeType::EXTRACTION_START)
{
  // Reset the log callback:
  setLogCallback({});

  // the default constructor would look for "7z.dll" on windows and
  // "/usr/lib/p7zip/7z.so" on linux
  try {
    m_Library = make_unique<Bit7zLibrary>(getLibraryPath());
    m_Valid   = true;
  } catch (const BitException& ex) {
    reportError("Could not find 7z library: "_L1 % ex.what());
    m_LastError = Error::ERROR_LIBRARY_NOT_FOUND;
  }
}

ArchiveImpl::~ArchiveImpl()
{
  close();
}

QString ArchiveImpl::errorString() const
{
  switch (m_LastError) {
  case Error::ERROR_NONE:
    return QStringLiteral("No error");
  case Error::ERROR_EXTRACT_CANCELLED:
    return QStringLiteral("Extract cancelled");
  case Error::ERROR_LIBRARY_NOT_FOUND:
    return QStringLiteral("Library not found");
  case Error::ERROR_LIBRARY_INVALID:
    return QStringLiteral("Library invalid");
  case Error::ERROR_ARCHIVE_NOT_FOUND:
    return QStringLiteral("Archive not found");
  case Error::ERROR_FAILED_TO_OPEN_ARCHIVE:
    return QStringLiteral("Failed to open archive");
  case Error::ERROR_INVALID_ARCHIVE_FORMAT:
    return QStringLiteral("Invalid archive format");
  case Error::ERROR_LIBRARY_ERROR:
    return QStringLiteral("Library error");
  case Error::ERROR_ARCHIVE_INVALID:
    return QStringLiteral("Archive invalid");
  case Error::ERROR_OUT_OF_MEMORY:
    return QStringLiteral("Out of memory");
  default:
    return QStringLiteral("Invalid error code");
  }
}

bool ArchiveImpl::open(std::filesystem::path const& archiveName,
                       PasswordCallback passwordCallback)
{
  if (!m_Valid) {
    if (m_LastError == Error::ERROR_LIBRARY_NOT_FOUND) {
      reportError(u"Could not open 7z library"_s);
    } else {
      reportError("Unknown error, id: "_L1 %
                  QString::number(static_cast<int>(m_LastError)));
    }
    return false;
  }

  // If it doesn't exist or is a directory, error
  if (!exists(archiveName) || is_directory(archiveName)) {
    m_LastError = Error::ERROR_ARCHIVE_NOT_FOUND;
    reportError(QStringLiteral("Archive file %1 not found").arg(archiveName.native()));
    return false;
  }

  try {
    if (BitArchiveReader::isHeaderEncrypted(
            *m_Library, to_tstring(archiveName.native()), BitFormat::Auto)) {
      m_Password = passwordCallback();
    }

    m_ArchivePtr =
        make_unique<BitArchiveReader>(*m_Library, to_tstring(archiveName.native()),
                                      BitFormat::Auto, toNative(m_Password));
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

    // set file changed callback
    if (m_FileChangeCallback) {
      m_FileChangeType = FileChangeType::EXTRACTION_START;

      m_ArchivePtr->setFileCallback([this](const tstring& path) {
        fileChangeCallbackWrapper(std::forward<fs::path>(fs::path(path)));
      });
    }

    // we could test the archive if we wanted to by calling
    // m_ArchivePtr->test();

    // Retrieve the list of indices we want to extract:
    vector<uint32_t> indices;

    // whether to use simple extraction
    bool allSimple = true;

    for (size_t i = 0; i < m_FileList.size(); ++i) {
      auto* fileData = dynamic_cast<FileDataImpl*>(m_FileList[i]);
      if (!fileData->isEmpty()) {
        indices.push_back(static_cast<uint32_t>(i));
        const auto& outputs = fileData->getOutputFilePaths();
        if (outputs.size() != 1 || outputs[0] != fileData->getArchiveFilePath()) {
          allSimple = false;
        }
      }
    }

    if (m_ProgressCallback) {
      m_ArchivePtr->setProgressCallback([this](uint64_t current) {
        return progressCallbackWrapper(current);
      });
    }

    if (allSimple) {
      m_ArchivePtr->extractTo(to_tstring(outputDirectory.native()), indices);
      for (auto* fileData : m_FileList) {
        fileData->clearOutputFilePaths();
      }
      return true;
    }

    std::error_code ec;

    const QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
      m_LastError = Error::ERROR_OUT_OF_MEMORY;
      reportError("Error creating a temporary directory for extraction, "_L1 %
                  tmpDir.errorString());
      return false;
    }

    const tstring tmpDirPath = toNative(tmpDir.path());

    // extract files into a temporary location
    m_ArchivePtr->extractTo(tmpDirPath, indices);

    // copy files to target locations
    for (auto* fileData : m_FileList) {
      // handle directories
      if (fileData->isDirectory()) {
        for (const auto& outputFilePath : fileData->getOutputFilePaths()) {
          fs::path targetDirectory = outputDirectory / outputFilePath;
          create_directories(targetDirectory, ec);
          if (ec) {
            m_LastError = Error::ERROR_LIBRARY_ERROR;
            reportError(QStringLiteral("Error creating output directory %1: %2")
                            .arg(targetDirectory.native(), ec.message()));
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
            reportError(QStringLiteral("Error creating output directory %1: %2")
                            .arg(targetDirectory.native(), ec.message()));
            return false;
          }
          // copy file
          copy_file(tmpDirPath / fileData->getArchiveFilePath(),
                    outputDirectory / outputFilePath, ec);
          if (ec) {
            m_LastError = Error::ERROR_LIBRARY_ERROR;
            reportError(
                QStringLiteral("Error writing to output file %1: %2")
                    .arg((outputDirectory / outputFilePath).native(), ec.message()));
            return false;
          }
        }
      }
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

void ArchiveImpl::reportError(const QString& message) const
{
  if (m_ErrorCallback) {
    m_ErrorCallback(message);
  } else {
    m_LogCallback(LogLevel::Error, message);
  }
}

tstring ArchiveImpl::passwordCallbackWrapper()
{
  // only ask for password once
  if (m_Password.isEmpty() && m_PasswordCallback) {
    m_Password = m_PasswordCallback();
  }
  return toNative(m_Password);
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
