#pragma once

#include <dlfcn.h>

using HMODULE = void*;

static inline constexpr DWORD ERROR_SUCCESS = 0;

inline void* LoadLibraryA(const char* path)
{
  return dlopen(path, RTLD_NOW);
}

inline void FreeLibrary(void* handle)
{
  dlclose(handle);
}

inline void* GetProcAddress(void* handle, const char* procName)
{
  return dlsym(handle, procName);
}

template <typename T>
T InterlockedIncrement(volatile T* a)
{
  return __sync_add_and_fetch(a, 1);
}

template <typename T>
T InterlockedDecrement(volatile T* a)
{
  return __sync_sub_and_fetch(a, 1);
}
