--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

-- Reactor pattern, wrapping a C++ EventBase
--
-- - loop()
--   Will block until some progress has been made (at least one callback
--   was called); returns the number of callbacks that have run.
--
-- - loop_nb()
--   Non-blocking version of loop(); returns immediately (and returns 0)
--   if no callbacks are runnable. Note that delayed callbacks are not
--   considered runnable until their timer has expired.
--
-- - run(cb, ...)
--   Add a callback to be called the next time loop() is invoked. Returns a
--   unique id that can be passed to remove_callback() or lookup_callback()
--
-- - run_after_delay(seconds, cb, ...)
--   Add a callback to be called when loop() is invoked, but no earlier than
--   the given number of seconds. Returns a unique id that can be passed
--   to remove_callback() or lookup_callback()
--
-- - remove_callback(id)
--   Removes the callback; returns the callback state prior to removing.
--
-- - lookup_callback(id)
--   Returns the callback state as a string (one of 'runnable', 'delayed')
--   or nil if the callback is not found
--
-- - await(f)
--   Wait for future f to be completed. Calls loop() underneath. Note that f
--   must be completed as a result of some callbacks scheduled to run in this
--   Reactor, or else you'll block forever.
--
-- Reactor is reentrant; if some callbacks call loop() (directly or via
-- await()), everything works as intended.
--
-- More interestingly, callbacks can be scheduled from C++ code (either
-- embedding Lua, or from a C++ module); get_executor() will return a
-- lightuserdata corresponding to a folly::Executor* that C++ code can use
-- for this purpose.

local util = require('fb.util')
local pl = require('pl.import_into')()
local cmod = require('fb.util.reactor_c')

local state_names = {
    [cmod.RUNNABLE] = 'runnable',
    [cmod.DELAYED] = 'delayed',
}

local M = {}

local Reactor = pl.class()

function Reactor:_init()
    self._reactor = cmod:new()
end

local function make_closure(cb, ...)
    local args = util.pack_n(...)
    return function() cb(util.unpack_n(args)) end
end

function Reactor:run(cb, ...)
    return self._reactor:add_callback(make_closure(cb, ...))
end

function Reactor:run_after_delay(seconds, cb, ...)
    return self._reactor:add_callback_delayed(seconds, make_closure(cb, ...))
end

function Reactor:remove_callback(id)
    return state_names[self._reactor:remove_callback(id)]
end

function Reactor:lookup_callback(id)
    return state_names[self._reactor:lookup_callback(id)]
end

function Reactor:loop()
    return self._reactor:loop(true)
end

function Reactor:loop_nb()
    return self._reactor:loop(false)
end

function Reactor:await(future)
    local done = false
    future:on_completion(function() done = true end)
    while not done do
        self:loop()
    end
    return future
end

function Reactor:awaitv(future)
    return self:await(future):value()
end

function Reactor:get_executor()
    return self._reactor:get_executor()
end

function Reactor:get_event_base()
    return self._reactor:get_event_base()
end

M.Reactor = Reactor

return M
