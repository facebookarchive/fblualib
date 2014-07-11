# fblualib C++ utilities

This module provides a few C++ utilities wrapping the Lua API.
`#include <fblualib/LuaUtils.h>`

The module provides a series of functions named (and organized) as follows:
for a type Foo (see below as to what types are allowed), we have:

* `luaGetFoo(lua_State* L, int index)` returns a `folly::Optional<Foo>`, which
  is set (non-empty) if the element on the stack at the given index can be
  converted to a Foo and empty otherwise
* `luaGetFooChecked(lua_State* L, int index)` returns a `Foo` if the element
  on the stack at the given index can be converted to a Foo, and raises a
  Lua error otherwise
* `luaGetFieldIfFoo(lua_State* L, int index, const char* field)` behaves
  similarly to `luaGetFoo`, but retrieves the data from a string-keyed field in
  a Lua table at the given index
* `luaGetFieldIfFooChecked(lua_State* L, int index, const char* field)`
  behaves similarly to `luaGetFooChecked`, but retrieves a field as above

Possible types (values of `Foo`) are:
* `String`; the value type is `folly::StringPiece`
* `Number<T>`, templated by a numeric type; the value type is `T` (so
  `luaGetNumber<float>(L, index)`, etc)
* `Tensor<T>`, templated by a numeric type; the value type is `thpp::Tensor<T>`
  (so `luaGetTensor<float>(L, index)`,  etc)

`luaGetString*` and `luaGetNumber*` support an extra `bool strict` argument
(default `false`) indicating whether the data must be of the exact expected
type (`strict = true`) or may be converted (`strict = false`); with
`strict = false`, we will convert between numbers and strings, and will
round floating-point values if integers are requested; with `strict = true`,
we won't.

There is also `luaPushTensor(lua_Stte* L, thpp::Tensor<T> tensor)` which
pushes a tensor on the stack (use the `lua_*` or `luaL_*` functions
from the Lua standard library for other types).
