#include "fileio.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <utime.h>

namespace IO
{

// FileBase

bool FileBase::Close() noexcept
{
  if (m_fd == -1)
    return true;
  if (close(m_fd) == -1)
    return false;
  m_fd = -1;
  return true;
}

bool FileBase::GetPosition(UInt64& position) noexcept
{
  return Seek(0, SEEK_CUR, position);
}

bool FileBase::GetLength(UInt64& length) const noexcept
{
  struct stat stats;
  if (fstat(m_fd, &stats) == -1) {
    return false;
  }
  length = stats.st_size;
  return true;
}

bool FileBase::Seek(UInt64 distanceToMove, UInt32 moveMethod,
                    UInt64& newPosition) const noexcept
{
  UInt64 result = lseek(m_fd, distanceToMove, moveMethod);
  if (result == -1) {
    return false;
  }
  newPosition = result;
  return true;
}

bool FileBase::Seek(UInt64 position, UInt64& newPosition) noexcept
{
  return Seek(position, SEEK_SET, newPosition);
}

bool FileBase::SeekToBegin() noexcept
{
  UInt64 newPosition;
  return Seek(0, newPosition);
}

bool FileBase::SeekToEnd(UInt64& newPosition) noexcept
{
  return Seek(0, SEEK_END, newPosition);
}

bool FileBase::Create(std::filesystem::path const& path, int flags, int mode) noexcept
{
  if (!Close()) {
    return false;
  }

  m_fd = open(path.c_str(), flags, mode);
  return m_fd != -1;
}

bool FileBase::GetFileInformation(std::filesystem::path const& path,
                                  FileInfo* info) noexcept
{
  // Use FileBase to open/close the file:
  FileBase file;
  if (!file.Create(path, O_RDONLY))
    return false;

  struct stat finfo;
  if (fstat(file.m_fd, &finfo) == -1) {
    return false;
  }

  *info = FileInfo(path, finfo);
  return true;
}

// FileIn

bool FileIn::Open(std::filesystem::path const& filepath) noexcept
{
  return Create(filepath.c_str(), O_RDONLY);
}

bool FileIn::Read(void* data, UInt32 size, UInt32& processedSize) noexcept
{
  processedSize = 0;
  do {
    UInt32 processedLoc = 0;
    bool res            = ReadPart(data, size, processedLoc);
    processedSize += processedLoc;
    if (!res)
      return false;
    if (processedLoc == 0)
      return true;
    data = data + processedLoc;
    size -= processedLoc;
  } while (size > 0);
  return true;
}
bool FileIn::Read1(void* data, UInt32 size, UInt32& processedSize) noexcept
{
  ssize_t result = read(m_fd, data, size);
  if (result == -1) {
    return false;
  }
  processedSize = result;
  return true;
}
bool FileIn::ReadPart(void* data, UInt32 size, UInt32& processedSize) noexcept
{
  if (size > kChunkSizeMax)
    size = kChunkSizeMax;
  return Read1(data, size, processedSize);
}

// FileOut

bool FileOut::Open(std::filesystem::path const& fileName) noexcept
{
  return Create(fileName, O_WRONLY | O_CREAT, 0666);
}

bool FileOut::SetTime(const timespec* cTime, const timespec* aTime,
                      const timespec* mTime) noexcept
{

  struct stat info;
  fstat(m_fd, &info);

  timespec times[2];

  times[0] = aTime == nullptr ? info.st_atim : *aTime;
  times[1] = mTime == nullptr ? info.st_mtim : *mTime;

  return futimens(m_fd, times) == 0;
}
bool FileOut::SetMTime(const timespec* mTime) noexcept
{
  return SetTime(nullptr, nullptr, mTime);
}
bool FileOut::Write(const void* data, UInt32 size, UInt32& processedSize) noexcept
{
  processedSize = 0;
  do {
    UInt32 processedLoc = 0;
    bool res            = WritePart(data, size, processedLoc);
    processedSize += processedLoc;
    if (!res)
      return false;
    if (processedLoc == 0)
      return true;
    data = (const void*)((const unsigned char*)data + processedLoc);
    size -= processedLoc;
  } while (size > 0);
  return true;
}

bool FileOut::SetLength(UInt64 length) noexcept
{
  UInt64 newPosition;
  if (!Seek(length, newPosition))
    return false;
  if (newPosition != length)
    return false;
  return SetEndOfFile();
}

bool FileOut::SetEndOfFile() noexcept
{
  UInt64 currentPosition;
  if (!GetPosition(currentPosition))
    return false;
  return ftruncate(m_fd, currentPosition) == 0;
}

bool FileOut::WritePart(const void* data, UInt32 size, UInt32& processedSize) noexcept
{
  if (size > kChunkSizeMax)
    size = kChunkSizeMax;
  UInt32 processedLoc = write(m_fd, data, size);
  if (processedLoc == -1) {
    return false;
  }
  processedSize = processedLoc;
  return true;
}

}  // namespace IO