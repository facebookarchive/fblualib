# fb-debugger: A source-level Lua debugger

This package implements a source-level Lua debugger.

## Usage

You may enter the debugger in two different ways:
* explicitly: at the point of interest, do
```lua
local debugger = require('fb.debugger')
debugger.enter()
```
  and you will be dropped in the debugger
* automatically when you hit an (uncaught) error: if using
  [fb.trepl](../trepl/README.md), you may set the environment variable
  `LUA_DEBUG_ON_ERROR` to `1`, and you'll be dropped in the debugger
  whenever your code raises an uncaught error.

## Debugger commands

`help` will give you a list of commands, inspired by
[gdb](http://www.gnu.org/software/gdb/). The following commands exist and behave
similarly to their gdb counterparts:
* `help` displays help
* `where` / `backtrace` / `bt` displays the current stack trace (with a
  marker for the currently selected frame)
* `frame` selects a given frame
* `up` / `down` moves the currently selected frame up / down one
* `b` / `break` sets a breakpoint at a given location (specified either as
  `<file>:<line_number>` or `<function_name>`; the function name is looked up
  in the scope of the current frame)
* `info breakpoints` lists breakpoints
* `enable`, `disable`, `delete` enable, disable, and delete a breakpoint,
  respectively
* `next` / `n` single-steps one line, skipping over function calls
* `step` / `s` single-steps one line, descending into function calls
* `finish` continues execution until the function in the currently selected
  frame returns
* `continue` / `c` continues program execution until the next breakpoint,
  or until the next time the debugger is reentered (via `debugger.enter()` or
  automatically in case of error)
* `locals` / `vlocals` shows locals in scope in the current frame; `vlocals`
  also shows values (verbose)
* `globals` / `vglobals` shows all globals
* `upvalues` / `vupvalues` shows the current function's upvalues
* `exec` / `e` executes code in the scope of the current frame
* `print` / `p` evaluates an expression in the scope of the current frame and
  prints the result
* `list` / `l` lists source code (if available); by default it lists the
  function in the current frame, but it accepts a location argument just like
  `break`; just like gdb, repeating `l` without arguments continues listing
  the same file
* `quit` / `q` quits the debugger; the program is resumed.

Note that `locals`, `globals`, or `upvalues` will occasionally show a
synthetic name for a variable (such as `_dbgl_tmp_4`). These indicate variables
that have been shadowed in the current scope (and so their original name
now refers to something else) or internal Lua temporaries (modifying those
is ill-advised).
