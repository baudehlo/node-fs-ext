#ifndef AUTORES_H
#define AUTORES_H

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdlib>
#include <cassert>
#include <new>

namespace autores {

// abstract class for wrappers of resources which need to be freed;
// the destructor disposes of the wrapped resource autmatically
//
// descendant class template:
//   template <class T> class ManagedResource :
//     public ManagedResource<Managed<T>, T> { ... };
// variable declaration:
//   ManagedResource<ResourceHandle> resource = OpenResource(...);
template <
  typename Derived,
  typename T
  >
class AutoRes {
  protected:
    T handle;

  public:
    // the default constructor; creates an empty wrapper
    AutoRes() : handle(Derived::InitialValue()) {}

    // initializing constructor; this object will own the handle
    AutoRes(T handle) : handle(handle) {}

    // ownership moving constructor; the source object will become empty
    // and this object will own its handle
    AutoRes(AutoRes & source) : handle(source.Detach()) {}

    // the destructor disposes of the wrapped handle, if it is valid
    ~AutoRes() {
      static_cast<Derived *>(this)->Dispose();
    }

    // ownership moving assignment operator
    Derived & operator =(Derived & source) {
      // dispose of the owned handle before hosting the new one
      static_cast<Derived *>(this)->Dispose();
      // read the handle from the source object and leave it empty
      // so that its destructor doesn't dispose of it when owned here
      handle = source.Detach();
      return static_cast<Derived &>(*this);
    }

    Derived & operator =(T source) {
      // dispose of the owned handle before hosting the new one
      static_cast<Derived *>(this)->Dispose();
      handle = source;
      return static_cast<Derived &>(*this);
    }

    // getting a pointer to the wrapper returns a pointer to the
    // the wrapped value to be able to ue it as output parameter
    // as well as the wrapped value alone would be
    T * operator &() {
      return &handle;
    }

    // the wrapper instance can be used as input parameter as well
    // as the wrapped value alone would be, thanks to this cast
    operator T() const {
      return handle;
    }

    // gets the wrapped handle in statements where the casting
    // operator is not practicable
    T Get() const {
      return handle;
    }

    // returns the wrapped handle and removes it from the wrapper; so that
    // when the wrapper is disposed of, the handle will stay intact
    T Detach() {
      T result = handle;
      // the wrapper is left with an invalid value
      handle = Derived::InitialValue();
      return result;
    }

    // disposes of the wrapped handle, if the handle is valid
    bool Dispose() {
      // proceed only if the wrapped handle is valid
      if (static_cast<Derived *>(this)->IsValid()) {
        // delegate the disposal to the descended class; the DisposeInternal
        // should be protected and AutoRes should be friend to the descendant
        static_cast<Derived *>(this)->DisposeInternal();
        // leave the wrapper with an invalid value to allow multiple calls
        // to this metod without failures
        handle = Derived::InitialValue();
      }
      return true;
    }

    // checks whether a valid handle is stored in the wrapper; more values
    // can be invalid, but at least one must be the InitialValue
    bool IsValid() const {
      return Derived::IsValidValue(handle);
    }

    // checks whether the specified handle is valid
    static bool IsValidValue(T handle) {
      return handle != Derived::InitialValue();
    }

    // returns an invalid handle value stored in an empty wrapper
    static T InitialValue() {
      return NULL;
    }
};

// abstract class for wrappers of allocated memory blocks
//
// descendant class template:
//   template <class T> class ManagedMemory :
//     public AutoMem<ManagedMemory<T>, T> { ... };
// variable declaration:
//   ManagedMemory<Item *> item = AllocateAndInitializeItem(...);
template <
  typename Derived,
  typename T
  >
class AutoMem : public AutoRes<
                   Derived, T
                 > {
  private:
    typedef AutoRes<
      Derived, T
    > Base;

  protected:
    typedef Base Res;

    bool DisposeInternal() {
      return Derived::Unallocate(Base::handle);
    }

    T & Dereference() {
      // the value in the dereferenced memory is being accessed;
      // it must not be posible if the wrapper is empty
      assert(static_cast<Derived *>(this)->IsValid());
      return Base::handle;
    }

    T const & Dereference() const {
      // the value in the dereferenced memory is being accessed;
      // it must not be posible if the wrapper is empty
      assert(static_cast<Derived const *>(this)->IsValid());
      return Base::handle;
    }

  public:
    AutoMem() {}

    AutoMem(T handle) : Base(handle) {}

    // ownership moving constructor
    AutoMem(AutoMem & source) : Base(source) {}

    Derived & operator =(Derived & source) {
      return Base::operator =(source);
    }

    Derived & operator =(T source) {
      return Base::operator =(source);
    }

    // chain the dereferencing operator to make the members of the
    // wrapped value accessible via the wrapper instance the same way
    T operator ->() {
      return Dereference();
    }
};

// wraps a pointer to an object allocated by new and disposed by delete
//
// variable declaration:
//   CppObj<Item *> item = new Item(...);
template <
  typename T
  >
class CppObj : public AutoMem<
                  CppObj<T>, T
                > {
  private:
    typedef AutoMem<
      CppObj<T>, T
    > Base;

    friend class Base::Res;

  public:
    CppObj() {}

    CppObj(T handle) : Base(handle) {}

    CppObj(CppObj & source) : Base(source) {}

    // ownership moving assignment operator
    CppObj & operator =(CppObj & source) {
      Base::operator =(source);
      return *this;
    }

    CppObj & operator =(T source) {
      Base::operator =(source);
      return *this;
    }

    static bool Unallocate(T handle) {
      delete handle;
      return true;
    }
};

#ifdef _WIN32
// wraps a handle to a kernel object which is disposed by CloseHandle
//
// variable declaration:
//   WinHandle<HANDLE> file = CreateFile(...);
template <
  typename T
  >
class WinHandle : public AutoRes<
                     WinHandle<T>, T
                   > {
  private:
    typedef AutoRes<
      WinHandle<T>, T
    > Base;

    friend class Base;

  protected:
    bool DisposeInternal() {
      return CloseHandle(Base::handle) != FALSE;
    }

  public:
    WinHandle() {}

    WinHandle(T handle) : Base(handle) {}

    WinHandle(WinHandle & source) : Base(source) {}

    // ownership moving assignment operator
    WinHandle & operator =(WinHandle & source) {
      Base::operator =(source);
      return *this;
    }

    WinHandle & operator =(T source) {
      Base::operator =(source);
      return *this;
    }

    static bool IsValidValue(T handle) {
      return Base::IsValidValue(handle) && handle != INVALID_HANDLE_VALUE;
    }
};

// wraps a pointer to memory allocated by LocalAlloc and disposed by LocalFree
//
// variable declaration:
//   LocalMem<Item *> memory = LocalAlloc(LMEM_FIXED, size);
template <
  typename T
  >
class LocalMem : public AutoMem<
                    LocalMem<T>, T
                  > {
  private:
    typedef AutoMem<
      LocalMem<T>, T
    > Base;

    friend class Base::Res;

  public:
    LocalMem() {}

    LocalMem(T handle) : Base(handle) {}

    LocalMem(LocalMem & source) : Base(source) {}

    // ownership moving assignment operator
    LocalMem & operator =(LocalMem & source) {
      Base::operator =(source);
      return *this;
    }

    LocalMem & operator =(T source) {
      Base::operator =(source);
      return *this;
    }

    static T Allocate(size_t size) {
      return (T) LocalAlloc(LMEM_FIXED, size);
    }

    static bool Unallocate(T handle) {
      return LocalFree(handle) == NULL;
    }
};

// wraps a pointer to memory allocated by GlobalAlloc and disposed by GlobalFree
//
// variable declaration:
//   GlobalMem<Item *> memory = GlobAlloc(GMEM_FIXED, size);
template <
  typename T
  >
class GlobalMem : public AutoMem<
                     GlobalMem<T>, T
                   > {
  private:
    typedef AutoMem<
      GlobalMem<T>, T
    > Base;

    friend class Base::Res;

  public:
    GlobalMem() {}

    GlobalMem(T handle) : Base(handle) {}

    GlobalMem(GlobalMem & source) : Base(source) {}

    // ownership moving assignment operator
    GlobalMem & operator =(GlobalMem & source) {
      Base::operator =(source);
      return *this;
    }

    GlobalMem & operator =(T source) {
      Base::operator =(source);
      return *this;
    }

    static T Allocate(size_t size) {
      return (T) GlobalAlloc(GMEM_FIXED, size);
    }

    static bool Unallocate(T handle) {
      return GlobalFree(handle) == NULL;
    }
};

// base class storing the heap which the memory block was allocated from;
// the descended class can set it or rely on the process heap by default
class HeapBase {
  private:
    static HANDLE processHeap;

  protected:
    mutable HANDLE heap;

  public:
    HeapBase() : heap(NULL) {}

    HeapBase(HANDLE heap) : heap(heap) {}

    HeapBase(HeapBase const & source) : heap(source.heap) {}

    HANDLE Heap() const {
      if (heap == NULL) {
        heap = ProcessHeap();
      }
      return heap;
    }

    static HANDLE ProcessHeap() {
      if (processHeap == NULL) {
        processHeap = GetProcessHeap();
      }
      return processHeap;
    }
};

// wraps a pointer to memory allocated by HeapAlloc and disposed by HeapFree
//
// variable declaration:
//   HeapMem<Item *> memory = HeapAlloc(GetProcessHeap(), 0, size);
template <
  typename T
  >
class HeapMem : public AutoMem<
                   HeapMem<T>, T
                 >,
                 public HeapBase {
  private:
    typedef AutoMem<
      HeapMem<T>, T
    > Base;

    friend class Base::Res;

  protected:
    bool DisposeInternal() {
      return Unallocate(Base::handle, HeapBase::Heap());
    }

  public:
    HeapMem() {}

    HeapMem(T handle) : Base(handle) {}

    HeapMem(T handle, HANDLE heap) : Base(handle), HeapBase(heap) {}

    HeapMem(HeapMem & source) : Base(source), HeapBase(source.heap) {}

    // ownership moving assignment operator
    HeapMem & operator =(HeapMem & source) {
      Base::operator =(source);
      heap = source.heap;
      return *this;
    }

    HeapMem & operator =(T source) {
      Base::operator =(source);
      return *this;
    }

    HeapMem & Assign(T source, HANDLE heap = NULL) {
      Base::operator =(source);
      heap = heap;
      return *this;
    }

    static T Allocate(size_t size, HANDLE heap = NULL) {
      if (heap == NULL) {
        heap = HeapBase::ProcessHeap();
      }
      return (T) HeapAlloc(heap, 0, size);
    }

    static bool Unallocate(T handle, HANDLE heap = NULL) {
      if (heap == NULL) {
        heap = HeapBase::ProcessHeap();
      }
      return HeapFree(heap, 0, handle) != FALSE;
    }
};
#endif // _WIN32

} // namespace autores

#endif // AUTORES_H
