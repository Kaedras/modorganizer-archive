/*
Mod Organizer archive handling

Copyright (C) 2012 Sebastian Herbord, 2020 MO2 Team. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "archive.h"

#include <bit7z/bitabstractarchivehandler.hpp>
#include <bit7z/bitarchivereader.hpp>
#include <bit7z/bitformat.hpp>
#include <bit7z/bitnestedarchivereader.hpp>

#include <atomic>
#include <filesystem>
#include <utility>

#ifdef _WIN32
static std::filesystem::path LIB = "dlls/7zip.dll";
static std::filesystem::path LIB_FALLBACK;
#else
static std::filesystem::path LIB = "lib/lib7zip.so";
static std::filesystem::path LIB_FALLBACK = "/usr/lib64/p7zip/7z.so";
#endif

using namespace bit7z;
using namespace std;

class FileDataImpl : public FileData
{
  friend class Archive;

public:
  FileDataImpl(std::filesystem::path fileName, uint64_t size, uint64_t crc, bool isDirectory)
      : m_FileName(std::move(fileName)), m_Size(size), m_CRC(crc), m_IsDirectory(isDirectory)
  {}

  [[nodiscard]] std::filesystem::path getArchiveFilePath() const override { return m_FileName; }
  [[nodiscard]] uint64_t getSize() const override { return m_Size; }

  void addOutputFilePath(std::filesystem::path const& fileName) override
  {
    m_OutputFilePaths.push_back(fileName);
  }
  [[nodiscard]] const std::vector<std::filesystem::path>& getOutputFilePaths() const override
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
  [[nodiscard]] const std::vector<FileData*>& getFileList() const override { return m_FileList; }
  bool extract(std::filesystem::path const& outputDirectory,
                       ProgressCallback progressCallback,
                       FileChangeCallback fileChangeCallback,
                       ErrorCallback errorCallback) override;

  void cancel() override;

private:
  void clearFileList();
  void resetFileList();
  void reportError(const native_string& message);

  // callback wrapper functions
  bool progressCallbackWrapper(uint64_t current); // returns true if we should continue extracting, false otherwise
  void fileChangeCallbackWrapper(const native_string& path);
  native_string passwordCallbackWrapper();

  bool m_Valid;
  bool m_Nested; // if we got a nested archive, e.g. tar.gz
  Error m_LastError;
  std::atomic<bool> m_shouldCancel = false;

  bit7z::Bit7zLibrary* m_Library;
  BitArchiveReader* m_ArchivePtr;

  Archive::ProgressType m_ProgressType;
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

Archive::LogCallback ArchiveImpl::DefaultLogCallback([](LogLevel, native_string const&) {
});

ArchiveImpl::ArchiveImpl()
   : m_Valid(false), m_Nested(false), m_LastError(Error::ERROR_NONE), m_ArchivePtr(nullptr), m_ProgressType(Archive::ProgressType::EXTRACTION), m_Total(0), m_FileChangeType(FileChangeType::EXTRACTION_START)
{
  // Reset the log callback:
  setLogCallback({});

  // the default constructor would look for "7z.dll" on windows and "/usr/lib/p7zip/7z.so" on linux
  try {
    m_Library = new Bit7zLibrary(LIB);
    m_Valid = true;
  } catch (const BitException& ex) {
    // try a second time using another library path
    try {
      m_Library = new Bit7zLibrary(LIB_FALLBACK);
      m_Valid = true;
    } catch (const BitException& ex) {
      m_LogCallback(LogLevel::Error, std::format("Caught exception {}.", ex.what()));
      m_LastError = Error::ERROR_LIBRARY_NOT_FOUND;
    }
  }
}

ArchiveImpl::~ArchiveImpl()
{
  close();
  delete m_Library;
}

bool ArchiveImpl::open(std::filesystem::path const& archiveName,
                       PasswordCallback passwordCallback)
{
  if(!m_Valid){
    switch(m_LastError){
    case Error::ERROR_LIBRARY_NOT_FOUND:
      m_LogCallback(LogLevel::Error, "Could not open 7z library");
      break;
    default:
      m_LogCallback(LogLevel::Error, "Unknown error, id: " + to_string((int)m_LastError));
    }
    return false;
  }

  // If it doesn't exist or is a directory, error
  if (!exists(archiveName) || is_directory(archiveName)) {
    m_LastError = Error::ERROR_ARCHIVE_NOT_FOUND;
    m_LogCallback(Archive::LogLevel::Error, "Archive not found");
    return false;
  }

  try {
    m_ArchivePtr = new BitArchiveReader(*m_Library, archiveName, BitFormat::Auto);

    m_PasswordCallback = passwordCallback;
    m_ArchivePtr->setPasswordCallback(bind(&ArchiveImpl::passwordCallbackWrapper, this));

    m_Total = m_ArchivePtr->size();

    m_LastError = Error::ERROR_NONE;
    resetFileList();
    return true;

  } catch ( const bit7z::BitException& ex ) {
    m_LastError = Error::ERROR_FAILED_TO_OPEN_ARCHIVE;
    m_LogCallback(Archive::LogLevel::Error, ex.what());
    return false;
  }
}

void ArchiveImpl::close()
{
  delete m_ArchivePtr;
  clearFileList();
  m_PasswordCallback = {};
}

void ArchiveImpl::clearFileList()
{
  for(FileData* iter : m_FileList) {
    delete dynamic_cast<FileDataImpl*>(iter);
  }
  m_FileList.clear();
}

void ArchiveImpl::resetFileList()
{
  try{
    clearFileList();

    m_FileList.reserve(m_ArchivePtr->itemsCount());

    for(const auto& item: *m_ArchivePtr){
      m_FileList.push_back(new FileDataImpl(item.path(),item.size(),item.crc(), item.isDir()));
    }

    // check if we got a nested archive
    if(m_FileList.size() == 1) {
      if(m_FileList[0]->getArchiveFilePath().extension() == ".tar"){
        m_Nested = true;
      }
    }
  }
  catch(...) {
    throw;
  }
}

bool ArchiveImpl::extract(const std::filesystem::path &outputDirectory,
                          Archive::ProgressCallback progressCallback,
                          Archive::FileChangeCallback fileChangeCallback,
                          Archive::ErrorCallback errorCallback) {
  if(!m_Valid) {
    return false;
  }

  try {
    m_ProgressCallback   = progressCallback;
    m_FileChangeCallback = fileChangeCallback;
    m_ErrorCallback = errorCallback;

    // set progress callback
    if(m_ProgressCallback) {
      m_ProgressType = ProgressType::EXTRACTION;

      m_ArchivePtr->setProgressCallback(
          bind(&ArchiveImpl::progressCallbackWrapper, this, placeholders::_1));
    }

    // set file changed callback
    if(m_FileChangeCallback) {
      m_FileChangeType = FileChangeType::EXTRACTION_START;

      m_ArchivePtr->setFileCallback(
          bind(&ArchiveImpl::fileChangeCallbackWrapper, this, placeholders::_1));
    }

    // we could test the archive if we wanted to by calling
    //m_ArchivePtr->test();

    // handle nested archive
    if(m_Nested) {
      m_LogCallback(LogLevel::Info, "Extracting nested archive");
      BitNestedArchiveReader nestedReader{*m_Library, *m_ArchivePtr, BitFormat::Tar};

      // add callbacks
      // tar does not support encryption, so we don't need to bother with a password callback
      nestedReader.setProgressCallback(m_ArchivePtr->progressCallback());
      nestedReader.setFileCallback(m_ArchivePtr->fileCallback());

      // NOTE: extracting a tar file can result in additional files like pax_global_header that are not always shown in other software
      nestedReader.extractTo(outputDirectory);
    } else {
      m_ArchivePtr->extractTo(outputDirectory);
    }

    return true;
  } catch (const BitException& ex) {
    if(m_shouldCancel){
      m_LastError = Error::ERROR_EXTRACT_CANCELLED;
    } else {
      m_LastError = Error::ERROR_LIBRARY_ERROR;
    }
    reportError(ex.what());
    return false;
  }
}

void ArchiveImpl::cancel() {
  m_shouldCancel.store(true);
}

void ArchiveImpl::reportError(const native_string& message) {
  if(m_ErrorCallback){
    m_ErrorCallback(message);
  }
}

native_string ArchiveImpl::passwordCallbackWrapper()
{
  // only ask for password once
  if(m_Password.empty() && m_PasswordCallback) {
    m_Password = m_PasswordCallback();
  }
  return m_Password;
}

bool ArchiveImpl::progressCallbackWrapper(uint64_t current)
{
  m_ProgressCallback(m_ProgressType, current, m_Total);
  return !m_shouldCancel;
}

void ArchiveImpl::fileChangeCallbackWrapper(const native_string& path)
{
  m_FileChangeCallback(m_FileChangeType, path);
}

DLLEXPORT std::unique_ptr<Archive> CreateArchive() {
  return std::make_unique<ArchiveImpl>();
}
