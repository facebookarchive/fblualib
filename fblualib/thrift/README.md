# fb-thrift: Thrift serialization for Lua objects

This module implements Thrift serialization for arbitrary Lua objects with
optional compression. Scalars, tables, functions with upvalues (serializing
bytecode), and torch Tensors are supported. The module supports serializing
arbitrary object graphs (with cycles), and the graph is restored properly on
deserialization.

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
local obj = thrift.from_file(obj)
```

`to_file` writes to the file at the current position of the file pointer,
and advances the file pointer past the serialized data. `from_file` reads
from the current file pointer and advances the file pointer past the serialized
data (that is, the format is self-delimiting, and you can serialize multiple
objects to the same file without any special framing).

Serialization functions (`to_file' and `to_string') accept an additional
argument that indicates the codec to use when compressing the data, if any.
Valid values are in the `thrift.codec` table: `NONE` (no compression), `LZ4`,
`SNAPPY`, `ZLIB`, `LZMA2` (but some might not be available if the corresponding
library wasn't installed on your system when
[folly](https://github.com/facebook/folly) was built).

## C++ interface
There is limited support for writing and reading Lua-serialized objects from
C++, without calling into Lua. Currently, only scalars and tensors are
supported. See `LuaObject.h` for details (you should be able to
include it as `<fblua/thrift/LuaObject.h>`)
