#ifndef WINWRAP_H
#define WINWRAP_H

#include <windows.h>
#include <tchar.h>

// duplicates a string using the LocalAlloc to allocate memory
LPTSTR LocalStrDup(LPCTSTR source);
// duplicates a string using the GlobalAlloc to allocate memory
LPTSTR GlobalStrDup(LPCTSTR source);
// duplicates a string using the HeapAlloc to allocate memory
LPTSTR HeapStrDup(HANDLE heap, LPCTSTR source);

#endif // WINWRAP_H
