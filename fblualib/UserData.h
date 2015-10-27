/**
 * Copyright 2015 Facebook
 * @author Tudor Bosman (tudorb@fb.com)
 */

#ifndef FBLUALIB_USERDATA_H_
#define FBLUALIB_USERDATA_H_

#include <fblualib/LuaUtils.h>

// C++ framework for writing OOP userdata objects.
//
// Userdata objects are full-fledged C++ objects, and Lua methods / metamethods
// are actual methods (non-static member functions) on the object.
//
// Usage:
//
// 1. Declare your object:
//
//   class MyClass {
//    public:
//     int luaLen(lua_State* L);
//     int luaFoo(lua_State* L);
//   };
//
// 2. Register your metamethods and methods, by defining a member of
// ::fblualib::Metatable<MyClass>:
//
//   namespace fblualib {
//
//   template <>
//   const UserDataMethod<MyClass> Metatable<MyClass>::methods[] = {
//     // Add methods and metamethods here.
//     //
//     // __gc will automatically be added to call the destructor.
//     //
//     // __index will be obeyed, but methods (below) take priority.
//
//     {"__len", &MyClass::luaLen},
//     // etc
//
//     {"foo", &MyClass::luaFoo},
//     // etc
//
//     {nullptr, nullptr}
//   };
//
//   }  // namespace fblualib
//
// 3. (Optional) If your class has a base class, you may declare this
// relationship between Lua objects as well. The derived class will inherit
// methods and metamethods from the parent, and getUserData<Base>(L, index)
// will work with an object of the derived type.
//
// You do this by specializing *at the place where your class is defined*
// (before you use any of the getUserData... etc functions)
//
//   namespace fblualib {
//
//   template <> struct BaseClass<MyDerived> {
//     typedef MyBase type;
//   };
//
//   }  // namespace fblualib
//
// Note that MyBase must actually be a base class of MyDerived.
//
// 4. Use the object. To push one onto the Lua stack, do:
//
//   // Construct an object of type MyClass from args, push it onto the
//   // Lua stack, and return a reference to it.
//   auto& obj = pushUserData<MyClass>(L, args...);
//
// Note that the objects are allocated on the Lua heap, so, if the data
// is large, you might want your objects to contain a unique_ptr to the
// actual data.
//
// Also in this file, helpers for the case where you only want to use this
// for lifetime management.
//
// - pushObject<T>(L, Args&&... args)
// - T* getObject<T>(L, index)
// - T& getObjectChecked<T>(L, index)

namespace fblualib {

template <class T>
struct UserDataMethod {
  const char* name;
  int (T::*method)(lua_State*);
};

// The methods (and metamethods) are class members. The first argument
// is automatically typechecked and passed as "this". The remaining
// arguments start at Lua stack index 2.

template <class T> struct Metatable {
  static const UserDataMethod<T> methods[];
};

template <class T> struct BaseClass {
  typedef void type;
};

// Push onto the stack the metatable for type T, creating it if not yet
// created.
template <class T>
int pushMetatable(lua_State* L);

// Return the object of type T at the given index, or nullptr if invalid
// (not an object, or object of wrong type)
template <class T>
T* getUserData(lua_State* L, int index);

// Return the object of type T at the given index; throw an error if invalid.
template <class T>
T& getUserDataChecked(lua_State* L, int index);

// Push onto the stack a newly-created object of type T, constructed from args.
template <class T, class... Args>
T& pushUserData(lua_State* L, Args&&... args);

// Register objects of type T to be used using the pushObject / getObject
// mechanism. Calling this multiple times is harmless.
template <class T>
int registerObject(lua_State* L);

// Create an object of type T and push it on the Lua stack.
template <class T, class... Args>
T& pushObject(lua_State* L, Args&&... args);

// Get a pointer to the object of type T stored at the given index,
// or nullptr if invalid.
template <class T>
T* getObject(lua_State* L, int index);

// Get a reference to the object of type T stored at the given index,
// exception if invalid.
template <class T>
T& getObjectChecked(lua_State* L, int index);

namespace detail {

template <class T>
int gcUserData(lua_State* L) {
  auto& obj = getUserDataChecked<T>(L, 1);
  obj.~T();
  return 0;
}

// Upvalue 1 is the index of the method in the method table.
template <class T>
int callUserDataMethod(lua_State* L) {
  auto& obj = getUserDataChecked<T>(L, 1);
  auto index = luaGetChecked<int>(L, lua_upvalueindex(1));
  auto method = Metatable<T>::methods[index].method;
  return (obj.*method)(L);
}

const char* const kIndexHandler = "_index_handler_";

template <class T>
void registerMethods(lua_State* L) {
  auto table = Metatable<T>::methods;

  // metatable methods
  for (int i = 0; table->name; ++table, ++i) {
    auto name =
      strcmp(table->name, "__index") ?
      table->name :
      kIndexHandler;
    luaPush(L, name);
    luaPush(L, i);
    lua_pushcclosure(L, callUserDataMethod<T>, 1);
    // If it starts with __, register in the metatable, otherwise in the
    // normal methods table.
    lua_settable(L, strncmp(name, "__", 2) ? -3 : -4);
  }

  // Point from metatable to methods table
  lua_pushvalue(L, -1);
  lua_setfield(L, -3, "_methods");
}

template <class Base, class T>
int castToBase(lua_State* L) {
  lua_pushlightuserdata(
      L,
      static_cast<Base*>(static_cast<T*>(lua_touserdata(L, 1))));
  return 1;
}

int indexTrampoline(lua_State* L);
void doRegisterBase(lua_State* L, lua_CFunction castFunc);

template <class T>
typename std::enable_if<
  std::is_base_of<typename BaseClass<T>::type, T>::value>::type
registerBase(lua_State* L) {
  typedef typename BaseClass<T>::type Base;
  // derived_mt derived_methods
  pushMetatable<Base>(L);
  doRegisterBase(L, &castToBase<Base, T>);
}

template <class T>
typename std::enable_if<
  std::is_same<typename BaseClass<T>::type, void>::value>::type
registerBase(lua_State* L) { }

template <class T>
int doCreateMetatable(lua_State* L) {
  lua_newtable(L);  // metatable
  lua_newtable(L);  // methods

  registerBase<T>(L);
  registerMethods<T>(L);
  // metatable methods

  // Add GC method
  lua_pushcfunction(L, &gcUserData<T>);
  lua_setfield(L, -3, "__gc");

  // If we have an __index metamethod, we need to go through a trampoline
  // that dispatches to either the methods table or the __index metamethod.
  // Otherwise, set __index to the methods table.

  // metatable methods
  lua_getfield(L, -1, kIndexHandler);
  if (!lua_isnil(L, -1)) {
    // metatable methods index_handler
    // Both methods and __index. We need to go through a trampoline.
    // set methods as upvalue #1
    // set index as upvalue #2
    lua_pushcclosure(L, indexTrampoline, 2);
    // metatable trampoline
  } else {
    lua_pop(L, 1);
  }

  // metatable <trampoline_or_methods>
  lua_setfield(L, -2, "__index");

  // metatable

  lua_pushlightuserdata(
      L,
      const_cast<void*>(static_cast<const void*>(&Metatable<T>::methods)));
  lua_pushvalue(L, -2);
  // metatable registry_key metatable
  lua_settable(L, LUA_REGISTRYINDEX);

  // metatable
  return 1;
}

void* findClass(lua_State* L, void* ptr);

}  // namespace detail

template <class T>
int pushMetatable(lua_State* L) {
  lua_pushlightuserdata(
      L,
      const_cast<void*>(static_cast<const void*>(&Metatable<T>::methods)));
  lua_gettable(L, LUA_REGISTRYINDEX);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    detail::doCreateMetatable<T>(L);
  }
  return 1;
}

template <class T>
T* getUserData(lua_State* L, int index) {
  index = luaRealIndex(L, index);
  auto ptr = lua_touserdata(L, index);
  if (!ptr) {
    return nullptr;  // not userdata
  }

  pushMetatable<T>(L);
  lua_getmetatable(L, index);

  return static_cast<T*>(detail::findClass(L, ptr));
}

template <class T>
T& getUserDataChecked(lua_State* L, int index) {
  auto ptr = getUserData<T>(L, index);
  if (!ptr) {
    luaL_error(L, "Invalid object (not userdata of expected type)");
  }
  return *ptr;
}

template <class T, class... Args>
T& pushUserData(lua_State* L, Args&&... args) {
  auto r = new (lua_newuserdata(L, sizeof(T))) T(std::forward<Args>(args)...);
  pushMetatable<T>(L);
  lua_setmetatable(L, -2);
  return *r;
}

namespace detail {

template <class T>
struct ObjectWrapper {
  template <class... Args>
  explicit ObjectWrapper(Args&&... args) : obj(std::forward<Args>(args)...) { }
  T obj;
};

}  // namespace detail

template <class T> struct Metatable<detail::ObjectWrapper<T>> {
  static const UserDataMethod<detail::ObjectWrapper<T>> methods[];
};

template <class T>
const UserDataMethod<detail::ObjectWrapper<T>>
Metatable<detail::ObjectWrapper<T>>::methods[] = {
  {nullptr, nullptr},
};

template <class T, class... Args>
T& pushObject(lua_State* L, Args&&... args) {
  auto& wrapper = pushUserData<detail::ObjectWrapper<T>>(
      L, std::forward<Args>(args)...);
  return wrapper.obj;
}

template <class T>
T* getObject(lua_State* L, int index) {
  auto wrapper = getUserData<detail::ObjectWrapper<T>>(L, index);
  return wrapper ? &wrapper->obj : nullptr;
}

template <class T>
T& getObjectChecked(lua_State* L, int index) {
  return getUserDataChecked<detail::ObjectWrapper<T>>(L, index).obj;
}

}  // namespaces

#endif /* FBLUALIB_USERDATA_H_ */
