# fb-util: various utilities that didn't fit anywhere else

This module contains a collection of various utilities and a "tracing"
library.

## `fb.util`: Assorted utilities
```lua
local util = require('fb.util')
```

### `util.Deque`: A deque class.

Supports the usual `push_front`, `pop_front`, `push_back`, and `pop_back`
operations. May be indexed using the usual Lua 1-based indexing, or with a
stable index that doesn't change when elements are added or removed.

```lua
local deq = util.Deque()
deq:push_back(10)
deq:push_back(20)
deq:push_back(30)
print(deq[1])     -- prints 10
print(deq[3])     -- prints 30
print(deq.first)  -- prints 1; the first element is at stable index 1
print(deq.last)   -- prints 3; the last element is at stable index 3
deq:pop_front()
print(deq[1])     -- prints 20
print(deq[2])     -- prints 30
print(deq:get_stable(2))  -- prints 20
print(deq.first)  -- prints 2
print(deq.last)   -- prints 3
```

### `util.set_default`: set default values on a table

Modifies a table to always return a default value for nonexistent keys
(similar to Python's `dict.get` method) and optionally store the default value
(similar to Python's `dict.setdefault` method).

```lua
local tab1 = {foo = 10, bar = 20}
local tab2 = {foo = 10, bar = 20}

-- Note that the default value must be a callback that produces a
-- new object every time, just like for Python's collections.defaultdict

-- default value not stored in table
util.set_default(tab1, function() return 100 end)
print(tab1.baz)  -- prints 100
for k,_ in pairs(tab1) print(k) end  -- prints foo and bar

-- default value stored in table on first access
util.set_default(tab2, function() return 100 end, true)
print(tab1.baz)  -- prints 100
for k,_ in pairs(tab2) print(k) end  -- prints foo, bar, and baz
```

### `util.time`, return (floating point) time since Epoch

This is similar to `os.time`, but it returns the time as a floating-point
number of seconds since Epoch (with microsecond precision).

### `util.sleep`, sleep for a number of seconds

Sleeps for a (floating point) number of seconds (with microsecond precision);
for example, `util.sleep(0.1)` sleeps for 100 ms.

### `util.random_seed`, returns a good random number seed

`math.randomseed(util.random_seed())`

### `util.create_temp_dir`, create a temporary directory

`util.create_temp_dir(prefix)` creates a temporary directory with a unique name
that starts with the given prefix; if `prefix` is an absolute path, its
dirname indicates where the directory will be created; otherwise, it will
be created in a system-specific location (usually `/tmp`).

```lua
-- Create a temp directory with default naming scheme; it will be named
-- starting with '/tmp/lua_temp.'
local name = util.create_temp_dir()

-- Create a temp directory in /usr/local/tmp with a name that starts with
-- 'foo'
local name = util.create_temp_dir('/usr/local/tmp/foo')
```

## `fb.util.error`: Error handling

Richer error handling facilities: `on_error` and `finally`.

* `on_error` allows you to specify an error handler (just like `xpcall`)
  that is called when a function raises an error. Unlike `xpcall`,
  the error continues to propagate to any handlers higher on the stack.
  (And unlike reraising the error from `xpcall` yourself, the backtrace
  at the point of the initial error is preserved)
* `finally` allows you to specify a handler that is always called,
  whether or not the function called raises an error. (If the handler is called
  due to an error, the error continues to propagate after the handler
  returns)

The syntax is the same as for (Lua 5.2) `xpcall`

```lua
local eh = require('fb.util.error')

-- Execute handler(err) if some_function(function_args...) throws
eh.on_error(some_function, handler, function_args...)

-- Execute handler(err) always, regardless of whether
-- function(function_args...) throws
eh.finally(some_function, handler, function_args...)
```

Note that this module redefines the standard `pcall` and `xpcall` functions
so they maintain some necessary internal state; this slows down error
processing (in our simple test, the cost of raising in a simple pcall
went from 120 to 600 nanoseconds, which is unlikely to be significant).

## `fb.util.trace`: Tracing

`fb.util.trace` implements a hierarchical tracing facility.

On one side, the tracing facility allows you to fire trace events identified by
a four-component key. On the other side, you can register "handlers" that are
called whenever events matching specified patterns fire.

At the core, the function trace() allows you to fire a trace event
("probe"), which is a `{key, args}` tuple.

The key is made up of 4 components in decreasing order of significance:
`DOMAIN`, `MODULE`, `FUNCTION`, and `EVENT`, separated by colon (`:`). The
meaning of the four components is somewhat arbitrary:

* `DOMAIN` specifies the high-level activity that your program is undertaking
(for example, if your program is training multiple neural nets in
sequence, this could be the name of the net being trained)
* `MODULE` identifies the library / module,
* `FUNCTION` identifies the location in the code (usually the name of a
function, appropriately qualified to distinguish it in the library)
* `EVENT` indicates the event within a function (arbitrary name, although
"entry" and "return" are used to signify entry and return to/from the
function, respectively; see trace_function() below)

`args` is an arbitrary tuple (although some probes have specific meanings for
args, see below).

The tracing facility sets `args.time` to the current time (as a floating point
number of seconds since Epoch).

`namespace()` will set the default first components of the key for the
duration of its callback; usually, this is used to set the `DOMAIN` around
calls into a module, and the `MODULE` within a module.

`trace_function()` wraps a function execution and fires two events, with the
`EVENT` components set to "entry" and "return", respectively.  "entry" sets
`args.args` to the list of arguments passed to the function; "return" sets
`args.ret` to the list of values returned by the function, or `args.error` to
the error string if the function threw an error. trace_function also tracks
function probe nesting level (which can be used for prettily indented output).

Callers may register "handlers" that will be called when certain probes
(matching specified patterns) fire. Handlers are registered using `add_handler`
and `remove_handler`; handlers are called as `handler(key, args,
nesting_level)`.

## `fb.util.dbg`: Environment-controlled debug output

Debugging code can be valuable, but verbose. The dbg module provides a
granular, hierarchical facility for selecting which debug output to produce
at runtime. The DBG environment variable consists of a comma-separated list
of module=threshold pairs. The dbg lua facility provides a factory for
module-specific print functions:

  local dbg = reqire('fb.util.dbg')
  local dprint = dbg.new('myModule')

  ...

  dprint(1, "sort of verbose") -- A
  dprint(10, "verbose")        -- B

Ordinarily the dprint's are silent. When the DBG environment variable is
set to 'myModule=1', the print statement labeled 'A' will start firing.
When it is set to 'myModule=100', both A and B will fire.

## `fb.util.timing`: Timing handler for tracing

This module implements a handler for the tracing module (see above) that
logs the probe key, time, and nesting level for all probes matching given
patterns. It can then print the log (with timing information) prettily
to standard output. See the comments in the code.
