#ifdef _WIN32

#include "autores.h"

namespace autores {

// the process heap will be initialized on the first usage
HANDLE HeapBase::processHeap = NULL;

} // namespace autores

#endif // _WIN32
