#ifdef _WIN32

#include "winwrap.h"
#include "autores.h"

#include <cassert>

using namespace autores;

// duplicates a string using the memory allocating method of a memory
// managing template descended from AutoMem
template <typename A> LPTSTR StrDup(LPCTSTR source) {
  assert(source != NULL);
  // the allocating method accepts byte size; not item count
  size_t size = (_tcslen(source) + 1) * sizeof(TCHAR);
  LPTSTR target = A::Allocate(size);
  if (target != NULL) {
    // copy including the terminating zero character
    CopyMemory(target, source, size);
  }
  return target;
}

// duplicates a string using the LocalAlloc to allocate memory
LPTSTR LocalStrDup(LPCTSTR source) {
  return StrDup<LocalMem<LPTSTR> >(source);
}

// duplicates a string using the GlobalAlloc to allocate memory
LPTSTR GlobalStrDup(LPCTSTR source) {
  return StrDup<GlobalMem<LPTSTR> >(source);
}

// duplicates a string using the HeapAlloc to allocate memory
LPTSTR HeapStrDup(HANDLE heap, LPCTSTR source) {
  assert(heap != NULL);
  assert(source != NULL);
  // the allocating method accepts byte size; not item count
  size_t size = (_tcslen(source) + 1) * sizeof(TCHAR);
  LPTSTR target = HeapMem<LPTSTR>::Allocate(size, heap);
  if (target != NULL) {
    // copy including the terminating zero character
    CopyMemory(target, source, size);
  }
  return target;
}

#endif // _WIN32
