--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

-- Reactor pattern, wrapping a C++ EventBase

local ffi = require('ffi')
local pl = require('pl.import_into')()
local util = require('fb.util')
local util_lib = util._clib

local M = {}

ffi.cdef([=[
void abort();
void* eventBaseNew();
void eventBaseDelete(void*);
bool eventBaseLoopForever(void*);
void eventBaseTerminateLoop(void*);
bool eventBaseRunInLoop(void*, void (*fn)(void));
bool eventBaseRunAfterDelay(void*, int milliseconds, void (*fn)(void));
]=])

local Callback = ffi.typeof('void (*)(void)')

local nullptr = ffi.new('void*')

local Reactor = pl.class()

-- Create a Reactor
function Reactor:_init()
    self._eb = ffi.gc(util_lib.eventBaseNew(), util_lib.eventBaseDelete)
    if self._eb == nullptr then
        error('Out of memory')
    end
end

-- Loop forever (until terminated with terminate_loop).
function Reactor:loop()
    -- This function may call deferred callbacks, so LuaJIT can't tell
    -- that it must not be compiled.
    jit.off(true, true)
    if not util_lib.eventBaseLoopForever(self._eb) then
        error('loop failed')
    end
end

-- Terminate the loop; call from within a callback to cause the containing
-- loop() to exit at the end of this loop.
function Reactor:terminate_loop()
    util_lib.eventBaseTerminateLoop(self._eb)
end

-- Wrap a Lua callback (with arguments); returns a FFI void (*)(void) that
-- will call the Lua callback when called. If the Lua callback throws an
-- error, the program aborts. The FFI callback frees itself when called,
-- so it may not be called more than once.
local function wrap_callback(cb, ...)
    local args = {...}
    local ffi_cb
    ffi_cb = ffi.cast(Callback, function()
        local function handler(err)
            io.stderr:write('Error in Reactor callback! ' .. err .. '\n')
            io.stderr:write(debug.traceback())
            ffi.C.abort()
        end
        local ok = xpcall(cb, handler, table.unpack(args))
        if not ok then
            io.stderr:write('Error in Reactor callback error handling?\n')
            ffi.C.abort()
        end
        ffi_cb:free()
    end)
    return ffi_cb
end
M.wrap_callback = wrap_callback

-- Return a pointer to the EventBase (as a FFI void*) to be used with C++ code
-- that needs to schedule callbacks. You may use wrap_callback() to wrap a
-- Lua callback into a void (*)(void). Note that scheduling callbacks from
-- another thread (via EventBase::runInEventBaseThread) is perfectly fine, as
-- long as the Lua clalback was wrapped in the correct LuaJIT thread.
function Reactor:event_base_ptr()
    return self._eb
end

-- Run a callback in the current or next iteration of the loop.
function Reactor:run(cb, ...)
    if not util_lib.eventBaseRunInLoop(self._eb, wrap_callback(cb, ...)) then
        error('Error in Reactor:call')
    end
end

-- Run a callback after a given delay (in seconds)
function Reactor:run_after_delay(delay, cb, ...)
    if not util_lib.eventBaseRunAfterDelay(self._eb, delay * 1000,
                                           wrap_callback(cb, ...)) then
        error('Error in Reactor:call_after_delay')
    end
end

M.Reactor = Reactor

return M
