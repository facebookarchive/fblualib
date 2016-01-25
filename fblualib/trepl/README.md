# fb-trepl: Read-Eval-Print loop

This module implements a Read-Eval-Print loop with command line editing and
pretty colorized output. It is based heavily on
[torch/trepl](https://github.com/torch/trepl).

Colorized output is on if the output is to a terminal and off otherwise;
behavior can be changed by the value of the `TORCH_COLOR` environment variable:
* `always` enables colorized output always (even when output is not to a
  terminal)
* `never` disable colorized output altogether

Usage: to run the default REPL:

```lua
local trepl = require('fb.trepl')
trepl.repl()
```

(from the command line, try `luajit -e "require('fb.trepl').repl()"`)

You may create additional REPLs that do not share state with the main REPL
(if any) -- useful if your program needs to accept arbitrary Lua input
and evaluate it:

```lua
local loop = trepl.make_repl()
loop()
```
