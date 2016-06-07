# fb-python: A bridge between Lua and Python

This modules allows almost seamless interaction between Lua and Python, with
on-the-fly conversion (with good defaults) of most data types back and forth.
This allows, for example, to use `numpy`, `scipy`, or `matplotlib` directly
with Torch tensors.

## Installation

Dependencies:

* glog
* boost
* python2.7 (Manually edit the CMake for older versions, 3+ is not supported)
* [thpp](https://github.com/facebook/thpp/) (If you don't have folly or thrift,
you can still build thpp with `THPP_NOFB=1 ./build.sh`, and fbpython should
still work)

```
luarocks make rockspec/*
```

## Usage

Throughout this document, we'll refer to this module as `py`:
```lua
local py = require('fb.python')
```

At the core, there are two main functions in this module:
* `py.exec(code, locals)` executes the given Python code (given as a string).
  The string is a sequence of statements and does not return a value (Python
  distinguishes between statements and expressions). If `locals` is specified,
  it must be a Lua table containing variables that the Python code will treat
  as local variables; if not specified, the code is executed at global (file)
  level (in the `__main__` module).
* `py.eval(code, locals)` evaluates the Python code and returns the result

Example:
```lua
py.exec([=[
import numpy as np
def foo(x):
  return x + 1
]=])

print(py.eval('foo(a) + 10', {a = 42}))  -- prints 53
```

There are a few things to note in the example above:
* Python is sensitive to indentation. When executing code, especially multiple
  lines, use the multiline string syntax: strings start with `[=...=[`, where
  you can have an arbitrary number of equal signs between the open brackets, and
  end with a matching `]=...=]` sequence with the same number of equal signs
  between the closed brackets.
* Values are converted magically between Lua and Python. The eval call creates
  a local Python environment with one variable (named `a`) that has the value
  42 (and Python `float` type); the return value (also a Python `float`) is
  converted to a Lua number. The exact conversion rules are detailed in the next
  sections.

## Data model

The Lua and Python data models do not match exactly, so conversions between Lua
and Python types are lossy (and heuristic). They should work well in most
cases, but you can override them by using opaque references (see below). Data
is transferred between Lua and Python **by value**; that is, variables on the
Python side are copies of the corresponding variables on the Lua side. Tables
are copied deeply; tensors share data (so mutating tensor elements **will** be
reflected back) but not metadata (so resizing **won't** be reflected back).

For example:
```lua
a = {foo = 'bar'}

py.exec([=[
a['foo'] = 'baz'
global b
b = a
]=], {a = a})
print(a['foo'])  -- prints bar; Python change not reflected back

a['foo'] = 'meow'

py.exec([=[
print(b['foo'])
]=])  -- prints baz; Lua change not reflected back
```

### Lua to Python conversions

* Lua numbers become Python `float`, unless they're used as indexes in a Lua
  table, in which case they become Python `int`. (If you use non-integral
  values as keys in your table, you deserve the pain, as floating point
  approximations will be your downfall)
* Lua strings become Python `str` objects, that is, strings of **bytes**, not
  characters. (In Python 3, these would be `bytes` objects). That is, they are
  not assumed to be valid in any character set; you may convert them to
  Unicode on the Python side if you wish.
* Lua `nil` becomes Python `None`; this one is obvious.
* Lua booleans become Python `bool`; this one is also obvious.
* Lua tables become Python `list` or `dict`, based on a heuristic: if
  `table[1]` exists but `table[0]` and `table[-1]` don't, we assume it's a
  `list`, otherwise it's a `dict`. (The `0` and `-1` checks were added to
  handle Lua's `arg` built-in table, which, unlike the Lua convention, uses `0`
  and negative indexes). Note that Python uses 0-based indexing, and Lua uses
  1-based indexing; the first element of a Lua list-like table is `x[1]` which
  becomes `x[0]` on the Python side. Regardless of whether a Lua table becomes a
  Python `list` or `dict`, numeric keys become `int`, not `float`.
* Torch tensors become `numpy.ndarray` objects with share data (so modifying a
  tensor element from Python is reflected back to Lua) but not metadata
  (so resizing the tensor won't be)

Note that Lua `nil` is different from Python `None`; there are some situations
where you may not use `nil` (you can't have `nil` values in a table, for
example -- that's the same as having no value for that key). In one of these
cases, if you want to pass `None` to Python, use `py.None`:
```lua
py.exec('print(a)', {a = nil})
-- won't print None; will complain about undefined variable 'a'
py.exec('print(a)', {a = py.None})
-- prints None
```

### Python to Lua conversions

Note that the **only** way to convert a Python object to Lua is to use
`py.eval`; this is true regardless of whether you use the default conversions
or opaque references (see below).

* Python numeric types (`int`, `long`, `float`, `numpy.float32`,
  `numpy.float64`, and other `numpy` scalars) become Lua numbers
* Python `str` objects (strings of **bytes**) become Lua strings
* Python `unicode` objects (strings of **characters**) are encoded using UTF-8
  and are then converted to Lua strings
* Python `None` becomes Lua `nil`
* Python `bool` objects become Lua booleans
* Python `list` and `tuple` objects become Lua tables; note that Python uses
  0-based indexing, while Lua uses 1-based indexing, so the first element of
  a Python `list` or `tuple` is `x[0]` which becomes `x[1]` on the Lua side.
* Python `dict` objects become Lua tables
* `numpy.ndarray` objects become Torch tensors which share data (so modifying
  a tensor element from Lua is reflected back to Python) but not metadata
  (so resizing the tensor won't be)

### Opaque references

Sometimes, the above conversions are not sufficient (you might want to force a
Lua number to be a Python `long`, or you might want to capture references to
Python objects of arbitrary types). In this case, we support opaque references,
which encapsulate any Python object. They can be used in place of Lua values
when passing arguments to Python:

```lua
local x = py.int(42)  -- x wraps a Python int, not float
py.exec('print(type(x))', {x = x}) -- prints <type 'int'>
```

You may create opaque references using the following functions:
* `py.import(module)` imports a Python module and returns an opaque reference
  to it
* `py.int(x)`, `py.long(x)`, `py.float(x)` convert a Lua number to a Python
  number of the appropriate type
* `py.str(x)` converts a Lua string to a Python `str`
* `py.unicode(x)` converts a Lua string to a Python `unicode` object, assuming
  UTF-8 encoding
* `py.tuple(x)`, `py.list(x)`, `py.dict(x)` convert a Lua table to a Python
  container of the appropriate type
* `py.ref(x)` converts an arbitrary Lua object to a Python reference (using the
  Lua-to-Python conversion rules shown above)
* `py.reval(code, locals)` is similar to `eval`, but returns an opaque reference
  rather than converting the result back to Lua; note that this allows you to
  create references to Python objects that can't be converted to Lua (such
  as objects of class types, modules, etc)

Also, `py.eval(ref)` converts an opaque reference to Lua, using the
Python-to-Lua conversion rules shown above.

Opaque references support function calls, arithmetic operations, attribute and
item lookup without going through `py.exec` or `py.eval`; these operations
are transparently bridged to Python and the results, if any, become
opaque references themselves (to allow chaining) but can be converted back to
Lua using `py.eval`.

Function calls on opaque references support variable and keyword arguments
using the `py.args` and `py.kwargs` magic placeholders. If `ref` is a
(callable) opaque reference, `args` is a list-like table, and `kwargs` is a
table, then
```lua
ref(arg1, arg2, arg3, py.args, args, py.kwargs, kwargs)
```
is the same as the Python call
```
ref(arg1, arg2, arg3, *args, **kwargs)
```

Example tying all this together:

```lua
-- np is opaque reference to Python numpy module
local np = py.import('numpy')

-- t1 is opaque reference to numpy.ndarray
local t1 = np.tri(10).transpose()

-- t2 is t1 converted to torch Tensor
local t2 = py.eval(t1)

local nltk = py.import('nltk')
local tokenized = py.eval(nltk.word_tokenize('Hello world, cats are awesome'))
```

(Again, note that operations on opaque references always return opaque
references, and you need `py.eval` at the end of an operation chain to
convert back to Lua)

## Automatic access to local variables

For convenience, there are functions `leval`, `lexec`, and `lreval` which do
the same as the corresponding functions without the `l` prefix, but they don't
take a `locals` second argument; they execute the Python code in the context of
the current variables visible from Lua. (That is, they convert all local and
global Lua variables visible at the point of the call, ignoring errors). This
is quite slow, and therefore should only be used interactively.
```lua
a = 42
py.lexec('print(a)')  -- prints 42.0
```

