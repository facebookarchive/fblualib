# fb-editline: [libedit](http://thrysoee.dk/editline/) bindings for LuaJIT

## Basic bindings
The core of this module is the `EditLine` class, instantiated with a
configuration table (described below). The `read` method returns the next
line read from the terminal, or `nil` if the user entered the EOF
character (usually Ctrl-D).

```lua
local editline = require('fb.editline')
local el_loop = editline.EditLine({
    prompt = 'hello> ',
})

while true do
  local line = el_loop:read()

  -- process line
  el_loop:add_history(line)
end
```

The configuration table contains:
* `prompt`: the prompt to display at the beginning of each line
* `prompt_func`: alternatively, this is a function called at the beginning
  of each line that should return the prompt; if `nil`, the value of
  `prompt` is used; if `prompt` is also `nil`, the prompt is `> '
* `history_file`: file name to save history to, if desired
* `word_break`: string of word break characters (good default)
* `auto_history`: if true, lines are automatically added to history;
  otherwise, call `add_history` yourself
* `complete`: completion function, called whenever the user types the
  completion key (by default Tab); `init.lua` has more details
* `max_matches`: the maximum number of matches to display when completing;
  if there are more matches, the user gets a confirmation prompt

## Word completion

A good completion function is provided:

```lua
local completer = require('fb.editline.completer')
local el_loop = editline.EditLine({
    prompt = 'hello> ',
    complete = completer.complete,
})
```

The completer completes values appropriately (local and global variables; Lua
keywords; if `foo` is a table / module, `foo.<tab>` will complete with keys in
`foo`) and strings as files in the filesystem (try `"./<tab>`)
