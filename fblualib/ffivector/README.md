# fb-ffivector: Vector of POD types or strings

This package implements a vector (1-dimensional array) of numbers, strings,
or other [FFI](http://luajit.org/ext_ffi.html) plain-old-data (POD) types.
These vectors (and their contents) do not count toward the Lua heap limit
(which is 1GiB in LuaJIT 2.0.x).

## Allowed element types

Any FFI cdata object is allowed, as long as it can be moved safely in
memory by calling `memmove()` (or equivalent); objects that store pointers to
(parts of) themselves cannot be used. Here's a contrived example of an invalid
object:

```c
struct Foo {
  int elements[100];
  int* currentElement;
};

Foo foo;
foo.currentElement = &(foo.elements[20]);
```

`foo.currentElement` points within `foo`, and therefore the pointer would
become invalid if `foo` were relocated to another address.

## Creating a vector

There is one main function (assuming `local ffivector = require('fb.ffivector')`):

```lua
ffivector.new(ctype, initial_capacity, index, newindex, destructor)
```

* `ctype` is the type of the elements, either a FFI ctype
(`ffi.typeof('double')`) or a string containing a C declaration (`double` or
`struct { double x; int y; }`)
* `initial_capacity` is the initial capacity of the vector, default 0
* `index` is an optional function that will be applied to elements as they
are retrieved _from_ the vector. For example, a vector of `int` could use
`tonumber`here, and then `v[42]` would return a Lua number rather than a FFI
`int`. The vector of strings implementation (see below) uses this to convert
from the internal representation to a Lua string (created on the fly).
* `newindex` is the opposite; it's an optional function that will be applied
to new values as they are added to the vector. The vector of strings
implementation uses this to convert from Lua strings to the internal
representation. Vectors of FFI numeric types don't need this, as LuaJIT
performs the conversion from Lua number to FFI numeric types automatically.
* `destructor` is an optional function that is called on each element as it is
destroyed (when the vector itself is GCed, or when an element is overwritten).
The vector of strings implementation uses this to free memory.

For convenience, we also provide functions to create vectors of standard
C numeric types: `new_char`, `new_unsigned_long`, `new_int`, `new_uint64_t`,
`new_float`, etc. The vectors such created have `tonumber` as the index
function, and so precision is limited by Lua numbers.

We also provide `new_string`, which creates a vector of strings. The strings
are stored outside the Lua heap, and a new Lua string is created (copied from
the vector) on demand whenever you access an element.

## Vector methods

Vectors support the `#` operator (returning the size, that is, the number
of elements currently in the vector), the usual (1-based) indexing, and the
following methods:

* `capacity()` returns the capacity (the number of currently allocated
elements); this may be greater than the vector's size.
* `reserve(n)` changes the vector's capacity to ensure that the size may grow
up to `n` elements without reallocation
* `resize(n)` grows (or shrinks) the vector so that its size is exactly `n`;
if `n` is greater than the current size, the new elments are initialized to 0;
if `n` is less than the current size, the elements past `n` are destroyed.

As a special case, we support assigning one element past the end of the vector,
which will grow the vector (as if by calling `vec:resize(#vec + 1)`), as
assigning one element past the end is a common pattern for growing Lua
list-like tables.
