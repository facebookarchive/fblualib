# Facebook port of [LuaUnit](http://phil.freehackers.org/programs/luaunit/)

The interface is very similar to LuaUnit, with a few minor differences:
* if the `LUAUNIT_LIST_TESTS` environment variable is set, LuaUnit will list
  tests rather than running them
* the `LUAUNIT_OUTPUT_TYPE` environment variable dictates the output format
