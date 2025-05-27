#include "fileio.h"

#include <sys/stat.h>

namespace IO
{

bool FileBase::Close() noexcept
{
  if (m_Handle == INVALID_HANDLE_VALUE)
    return true;
  if (!::CloseHandle(m_Handle))
    return false;
  m_Handle = INVALID_HANDLE_VALUE;
  return true;
}

bool FileBase::GetLength(UInt64& length) const noexcept
{
  DWORD sizeHigh;
  DWORD sizeLow = ::GetFileSize(m_Handle, &sizeHigh);
  if (sizeLow == 0xFFFFFFFF)
    if (::GetLastError() != NO_ERROR)
      return false;
  length = (((UInt64)sizeHigh) << 32) + sizeLow;
  return true;
}

bool FileBase::Seek(Int64 distanceToMove, DWORD moveMethod,
                    UInt64& newPosition) noexcept
{
  LONG high = (LONG)(distanceToMove >> 32);
  DWORD low = ::SetFilePointer(m_Handle, (LONG)(distanceToMove & 0xFFFFFFFF), &high,
                               moveMethod);
  if (low == 0xFFFFFFFF)
    if (::GetLastError() != NO_ERROR)
      return false;
  newPosition = (((UInt64)(UInt32)high) << 32) + low;
  return true;
}

bool FileBase::Create(std::filesystem::path const& path, DWORD desiredAccess,
                      DWORD shareMode, DWORD creationDisposition,
                      DWORD flagsAndAttributes) noexcept
{
  if (!Close()) {
    return false;
  }

  m_Handle =
      ::CreateFileW(path.c_str(), desiredAccess, shareMode, (LPSECURITY_ATTRIBUTES)NULL,
                    creationDisposition, flagsAndAttributes, (HANDLE)NULL);

  return m_Handle != INVALID_HANDLE_VALUE;
}

bool FileBase::GetFileInformation(std::filesystem::path const& path,
                                  FileInfo* info) noexcept
{
  // Use FileBase to open/close the file:
  FileBase file;
  if (!file.Create(path, 0, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS))
    return false;

  BY_HANDLE_FILE_INFORMATION finfo;
  if (!BOOLToBool(GetFileInformationByHandle(file.m_Handle, &finfo))) {
    return false;
  }

  *info = FileInfo(path, finfo);
  return true;
}

// FileIn

bool FileIn::Open(std::filesystem::path const& filepath, DWORD shareMode,
                  DWORD creationDisposition, DWORD flagsAndAttributes) noexcept
{
  bool res = Create(filepath.c_str(), GENERIC_READ, shareMode, creationDisposition,
                    flagsAndAttributes);
  return res;
}
bool FileIn::OpenShared(std::filesystem::path const& filepath,
                        bool shareForWrite) noexcept
{
  return Open(filepath, FILE_SHARE_READ | (shareForWrite ? FILE_SHARE_WRITE : 0),
              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
}

}  // namespace IO