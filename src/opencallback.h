/*
Mod Organizer archive handling

Copyright (C) 2012 Sebastian Herbord. All rights reserved.

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


#ifndef OPENCALLBACK_H
#define OPENCALLBACK_H

#include "callback.h"
#include "unknown_impl.h"

#include "7zip/Archive/IArchive.h"
#include "7zip/IPassword.h"

#include "fileio.h"

#include <filesystem>
#include <string>

class CArchiveOpenCallback: public IArchiveOpenCallback,
                            public IArchiveOpenVolumeCallback,
                            public ICryptoGetTextPassword,
                            public IArchiveOpenSetSubArchiveName
{

  UNKNOWN_4_INTERFACE(IArchiveOpenCallback,
                      IArchiveOpenVolumeCallback,
                      ICryptoGetTextPassword,
                      IArchiveOpenSetSubArchiveName);

public:

  CArchiveOpenCallback(PasswordCallback passwordCallback, std::filesystem::path const &filepath);

  ~CArchiveOpenCallback() { }

  const std::wstring& GetPassword() const { return m_Password; }

  INTERFACE_IArchiveOpenCallback(;)
  INTERFACE_IArchiveOpenVolumeCallback(;)

  // ICryptoGetTextPassword interface
  STDMETHOD(CryptoGetTextPassword)(BSTR *password);
  //Not implemented STDMETHOD(CryptoGetTextPassword2)(Int32 *passwordIsDefined, BSTR *password);

  // IArchiveOpenSetSubArchiveName interface
  STDMETHOD(SetSubArchiveName)(const wchar_t *name);

private:

  PasswordCallback m_PasswordCallback;
  std::wstring m_Password;

  std::filesystem::path m_Path;
  IO::FileInfo m_FileInfo;

  bool m_SubArchiveMode;
  std::wstring m_SubArchiveName;

};

#endif // OPENCALLBACK_H
