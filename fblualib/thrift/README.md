# fb-thrift: Thrift serialization for Lua objects

This module implements Thrift serialization for arbitrary Lua objects with
optional compression. Scalars, tables, functions with upvalues (serializing
bytecode), and torch Tensors are supported. The module supports serializing
arbitrary object graphs (with cycles), and the graph is restored properly on
deserialization.

There is additional support for Torch objects (created with `torch.class`) and
[Penlight](http://stevedonovan.github.io/Penlight/api/index.html) objects
(`pl.class`); see below.

## Lua interface
```lua
local thrift = require('fb.thrift')

local obj = { foo = 2 }  -- arbitrary Lua object

-- Serialization

-- to Lua string
local str = thrift.to_string(obj)

-- to open io.file object
local f = io.open('/tmp/foo', 'wb')
thrift.to_file(obj, f)

-- Deserialization

-- from Lua string
local obj = thrift.from_string(str)

-- from open io.file object
local f = io.open('/tmp/foo')
local obj = thrift.from_file(f)
```

`to_file` writes to the file at the current position of the file pointer,
and advances the file pointer past the serialized data. `from_file` reads
from the current file pointer and advances the file pointer past the serialized
data (that is, the format is self-delimiting, and you can serialize multiple
objects to the same file without any special framing).

Serialization functions (`to_file' and `to_string') accept an additional
argument that indicates the codec to use when compressing the data, if any.
Valid values are in the `thrift.codec` table: `NONE` (no compression, default),
`LZ4`, `SNAPPY`, `ZLIB`, `LZMA2` (but some might not be available if the
corresponding library wasn't installed on your system when
[folly](https://github.com/facebook/folly) was built).

## OOP support

There is additional support for Object-Oriented Programming using
`torch.class`, Penlight's `pl.class`, and adding other OOP schemes is easy.

For Torch objects, the class is correctly identified at serialization time and
restored at deserialization time.

For Penlight objects, you must explicitly add all classes that you want
to be able to serialize with `thrift.add_penlight_class(cls, name)`, where
`cls` is the class to serialize, and `name` is a globally unique name;
when deserializing, you must call `add_penlight_class` with the same
class -- name mapping. You may add multiple Penlight classes at once
by using `add_penlight_classes`; see the documentation in the Lua file for
details.

For both Torch and Penlight classes, if the class has methods named
`_thrift_serialize` and `_thrift_deserialize`, these methods will be called
at serialization and deserialization time, respectively, as follows:

At serialization time, before an object of that class is serialized, we call
`obj:_thrift_serialize()`. This method must either return a table (that will
be serialized *instead of* `obj`) or `nil` (which means we'll serialize `obj`;
this gives you an opportunity to blow away temporary fields that shouldn't be
serialized.)

At deserialization time, we call `_thrift_deserialize(obj)` which must mutate
obj *in place*. `obj` is a table (not yet blessed as a Torch or Penlight
class), and `_thrift_deserialize` must set fields appropriately so that it can
be blessed and produce a valid object; we'll then bless the object by making
it of the correct class.

Adding support for other OOP schemes is easy; see the documentation for
`add_metatable` in the Lua file.

## C++ interface
There is limited support for writing and reading Lua-serialized objects from
C++, without calling into Lua. Currently, only scalars and tensors are
supported. See `LuaObject.h` for details (you should be able to
include it as `<fblua/thrift/LuaObject.h>`)
