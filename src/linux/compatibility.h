#pragma once

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
