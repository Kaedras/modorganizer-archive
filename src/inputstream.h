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

#ifndef INPUTSTREAM_H
#define INPUTSTREAM_H

#include "7zip/IStream.h"

#include <filesystem>

#include "fileio.h"
#include "unknown_impl.h"

#ifdef __unix__
#include "linux/compatibility.h"
#endif

/** This class implements an input stream for opening archive files
 *
 * Note that the handling on errors could be better.
 */
class InputStream : public IInStream
{

  UNKNOWN_1_INTERFACE(IInStream);

public:
  InputStream();

  virtual ~InputStream();

  bool Open(std::filesystem::path const& filename);

  STDMETHOD(Read)(void* data, UInt32 size, UInt32* processedSize) noexcept;
  STDMETHOD(Seek)(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) noexcept;

private:
  IO::FileIn m_File;
};

#endif  // INPUTSTREAM_H
