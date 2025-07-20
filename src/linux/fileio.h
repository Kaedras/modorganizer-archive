#ifndef FILEIO_H
#define FILEIO_H

#include <Common/MyWindows.h>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <utility>

namespace IO
{
/**
 * Small class that wraps stat and returns
 * type matching 7z types.
 */
class FileInfo
{
public:
  FileInfo() : m_Valid{false}, m_FileInfo() {}
  FileInfo(std::filesystem::path path, const struct stat& fileInfo)
      : m_Valid{true}, m_Path(std::move(path)), m_FileInfo{fileInfo}
  {}

  bool isValid() const { return m_Valid; }

  const std::filesystem::path& path() const { return m_Path; }

  __mode_t fileAttributes() const { return m_FileInfo.st_mode; }
  timespec creationTime() const { return m_FileInfo.st_ctim; }
  timespec lastAccessTime() const { return m_FileInfo.st_atim; }
  timespec lastWriteTime() const { return m_FileInfo.st_mtim; }
  uint32_t device() const { return m_FileInfo.st_dev; }
  uint64_t fileSize() const { return m_FileInfo.st_size; }
  uint32_t numberOfLinks() const { return m_FileInfo.st_nlink; }
  uint64_t fileIndex() const { return m_FileInfo.st_ino; }

  // bool isArchived() const { return MatchesMask(FILE_ATTRIBUTE_ARCHIVE); }
  // bool isCompressed() const { return MatchesMask(FILE_ATTRIBUTE_COMPRESSED); }
  bool isDir() const { return IsType(S_IFDIR); }
  // bool isEncrypted() const { return MatchesMask(FILE_ATTRIBUTE_ENCRYPTED); }
  bool isHidden() const { return m_Path.filename().string().starts_with("."); }
  bool isNormal() const { return IsType(S_IFREG); }
  // bool isOffline() const { return MatchesMask(FILE_ATTRIBUTE_OFFLINE); }
  // bool isReadOnly() const { return MatchesMask(FILE_ATTRIBUTE_READONLY); }
  // bool iasReparsePoint() const { return MatchesMask(FILE_ATTRIBUTE_REPARSE_POINT); }
  // bool isSparse() const { return MatchesMask(FILE_ATTRIBUTE_SPARSE_FILE); }
  // bool isSystem() const { return MatchesMask(FILE_ATTRIBUTE_SYSTEM); }
  // bool isTemporary() const { return MatchesMask(FILE_ATTRIBUTE_TEMPORARY); }

private:
  bool IsType(mode_t type) const { return (m_FileInfo.st_mode & S_IFMT) == type; }

  bool m_Valid;
  std::filesystem::path m_Path;
  struct stat m_FileInfo;
};

class FileBase
{
public:  // Constructors, destructor, assignment.
  FileBase() noexcept : m_fd{-1} {}

  FileBase(FileBase&& other) noexcept : m_fd{other.m_fd} { other.m_fd = -1; }

  ~FileBase() noexcept { Close(); }

  FileBase(FileBase const&)            = delete;
  FileBase& operator=(FileBase const&) = delete;
  FileBase& operator=(FileBase&&)      = delete;

public:  // Operations
  bool Close() noexcept;

  bool GetPosition(UInt64& position) noexcept;
  bool GetLength(UInt64& length) const noexcept;

  bool Seek(UInt64 distanceToMove, UInt32 moveMethod,
            UInt64& newPosition) const noexcept;
  bool Seek(UInt64 position, UInt64& newPosition) noexcept;
  bool SeekToBegin() noexcept;
  bool SeekToEnd(UInt64& newPosition) noexcept;

  // Note: Only the static version (unlike in 7z) because I want FileInfo to hold the
  // path to the file, and the non-static version is never used (except by the static
  // version).
  static bool GetFileInformation(std::filesystem::path const& path,
                                 FileInfo* info) noexcept;

protected:
  bool Create(std::filesystem::path const& path, int flags, int mode = 0) noexcept;

protected:
  static constexpr uint32_t kChunkSizeMax = (1 << 22);

  int m_fd;
};

class FileIn : public FileBase
{
public:
  using FileBase::FileBase;

public:  // Operations
  bool Open(std::filesystem::path const& filepath) noexcept;

  bool Read(void* data, UInt32 size, UInt32& processedSize) noexcept;

protected:
  bool Read1(void* data, UInt32 size, UInt32& processedSize) noexcept;
  bool ReadPart(void* data, UInt32 size, UInt32& processedSize) noexcept;
};

class FileOut : public FileBase
{
public:
  using FileBase::FileBase;

public:  // Operations:
  bool Open(std::filesystem::path const& fileName) noexcept;

  bool SetTime(const timespec* cTime, const timespec* aTime,
               const timespec* mTime) noexcept;
  bool SetMTime(const timespec* mTime) noexcept;
  bool SetMTime(const FILETIME* mTime) noexcept;
  bool Write(const void* data, UInt32 size, UInt32& processedSize) noexcept;

  bool SetLength(UInt64 length) noexcept;
  bool SetEndOfFile() noexcept;

protected:  // Protected Operations:
  bool WritePart(const void* data, UInt32 size, UInt32& processedSize) noexcept;
};

inline std::filesystem::path make_path(std::wstring const& pathstr)
{
  return pathstr;
}

inline std::filesystem::path make_path(std::string const& pathstr)
{
  return pathstr;
}

}  // namespace IO

#endif  // FILEIO_H
