#ifndef FS_WIN_H
#define FS_WIN_H

#include <node.h>
#include <v8.h>

namespace fs_win {

using namespace node;
using namespace v8;

// to be called during the node add-on initialization
void init(Handle<Object> target);

} // namespace fs_win

#endif // FS_WIN_H
