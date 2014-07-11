--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

-- Richer error handling facilities.
--
-- The standard Lua pcall / xpcall functions allow some manner of error
-- processing, however they do not allow multiple error handlers to be stacked.
-- This is problematic when trying to implement RAII-style mechanisms.
--
-- Also, pcall and xpcall lose the error backtrace unless we save it from
-- the error handler in some application-specific way. This means that using
-- pcall / xpcall in order to do cleanup on error leads to the backtrace
-- being lost.
--
-- This module provides a few different primitives. It redefines
-- pcall and xpcall (really!) in order to keep additional state.
--
-- Other than pcall and xpcall, this module also defines on_error and
-- finally:
--
-- on_error(func, handler, ...)
--   execute func(...), execute handler(err) on error. The error propagates as
--   normal to the next pcall / xpcall on the stack. Returns the value
--   returned by func.
--
-- finally(func, handler, ...)
--   execute func(...), execute handler(err) on error, execute handler()
--   on success. The error propagates as normal to the next pcall / xpcall
--   on the stack. Returns the value returned by func.

local M = {}

-- Implementation details:
--
-- For each Lua stack (coroutine), we keep a stack of nested try/catch blocks
-- (aka pcalls). Each element of the stack is a stack of pcstack to call
-- (one for each pcall/on_error/finally at that level).

local main_pcstack = {}
local coroutine_pcstack = {}  -- keyed by coroutine id
setmetatable(coroutine_pcstack, {__mode = 'k'})  -- weak keys

-- Save the actual pcall and xpcall functions
local real_pcall = pcall
local real_xpcall = xpcall

local function current_pcstack()
    local co = coroutine.running()
    if co then
        local h = coroutine_pcstack[co]
        if not h then
            h = {}
            coroutine_pcstack[co] = h
        end
        return h
    else
        return main_pcstack
    end
end

local function current_handlers()
    local pcstack = current_pcstack()
    assert(#pcstack > 0)
    return pcstack[#pcstack]
end

local function error_handler(err)
    local handlers = current_handlers()
    for i = #handlers, 1, -1 do
        local handler = handlers[i]
        err = handler(err)
    end

    return err
end

local function _xpcall(func, handlers, ...)
    local pcstack = current_pcstack()
    table.insert(pcstack, handlers)
    local result = {real_xpcall(func, error_handler, ...)}
    assert(pcstack[#pcstack] == handlers)
    table.remove(pcstack)
    return unpack(result)
end

function pcall(func, ...)
    return _xpcall(func, {}, ...)
end

function xpcall(func, handler, ...)
    return _xpcall(func, {handler}, ...)
end

-- Ensure that we have at least one pcall() block on the stack, so we
-- can attach our on_error handler to it. If we don't, create one, in
-- which case we won't be able to preserve the backtrace on error.
local function ensure_pcall(func, ...)
    local pcstack = current_pcstack()
    if #pcstack == 0 then
        local r = {pcall(func, ...)}
        if not r[1] then
            error(r[2])
        end
        return unpack(r, 2)
    else
        return func(...)
    end
end

local function _on_error(func, handler, run_on_success, ...)
    local pcstack = current_pcstack()
    local handlers = pcstack[#pcstack]

    local function ehandler(err)
        return handler(err) or err
    end

    table.insert(handlers, ehandler)
    local r = {func(...)}
    assert(handlers[#handlers] == ehandler)
    table.remove(handlers)
    if run_on_success then
        if not pcall(handler) then
            error('Error in error handling!')
        end
    end
    return unpack(r)
end

function M.on_error(func, handler, ...)
    return ensure_pcall(_on_error, func, handler, false, ...)
end

function M.finally(func, handler, ...)
    return ensure_pcall(_on_error, func, handler, true, ...)
end

return M
