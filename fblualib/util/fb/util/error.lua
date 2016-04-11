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
--
--
-- Structured error handling
--
-- error() allows you to throw anything in Lua, with no structure (in
-- particular, no stack trace). This module allows you to wrap such errors
-- into structured objects which capture the stack trace at the point of
-- creation and also support nesting (an error that was generated while
-- processing another error).
--
-- Wrapped error objects have a few accessible fields:
--
-- message:      the original error message
-- message_str:  the error message as a string
-- nested_error: the nested error, if any
-- traceback:    the traceback captured at construction time
--
-- wrap(e, level)
--   Wrap 'e', capturing the stack trace (unless already wrapped).
--   Use in error handlers:
--
--   local function handler(err)
--       -- do something
--       return eh.wrap(err)
--   end
--   xpcall(func, handler, args...)
--
--   Or, as a substitute for pcall(func, args...), use wrap() as the handler
--   directly: xpcall(func, eh.wrap, args...)
--
-- create(message, nested_error, level)
--   Create a wrapped error object with the given message, and, if specified,
--   indicating that the error was caused by another (nested) error.
--   Use when creating new error objects rather than wrapping caught errors.
--   (There are slight semantic differences; error() prepends information about
--   the topmost level of the call stack to the error string, and so create()
--   does the same; wrap() assumes that the error being wrapped has already
--   been processed by error())
--
-- throw(message, nested_error, level)
--   Shortcut for error(create(message, nested_error, level+1))
--
-- format(e)
--   Format an error message. Always returns a string, whether or not e
--   is wrapped.
--
-- The level argument indicates the first level of the call stack to include in
-- the stack trace; the default is 1, the function calling wrap() / create() /
-- throw(). Just like the builtin error(), create() and throw() also accept
-- level == 0, in which case they won't adorn the error message with
-- information about the call stack.
--
--
-- Other useful functions
--
-- safe_tostring(obj)
--   Return a string description of the object. Won't throw; will return
--   an explanatory message if tostring(obj) throws.

local pl = require('pl.import_into')()
local util = require('fb.util')

local M = {}

local function safe_tostring(msg)
    local ok, str = pcall(tostring, msg)
    if ok then return str end
    ok, str = pcall(tostring, str)
    if ok then return '(cannot convert to string: ' .. str .. ')' end
    return '(cannot convert to string: failed recursively)'
end
M.safe_tostring = safe_tostring

-- Implementation details:
--
-- For each Lua stack (coroutine), we keep a stack of nested try/catch blocks
-- (aka pcalls). Each element of the stack is a stack of pcstack to call
-- (one for each pcall/on_error/finally at that level).

local main_pcstack = {}
local coroutine_pcstack = {}  -- keyed by coroutine id
setmetatable(coroutine_pcstack, {__mode = 'k'})  -- weak keys

-- Save the actual pcall and xpcall functions
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

-- Wrapped errors
local WrappedError = pl.class()

function WrappedError:_init(msg, level)
    level = level or 1
    self.message = msg
    self.message_str = safe_tostring(msg)
    -- Penlight class constructors are nested two levels deep.
    -- Strip those, and strip _init().
    self.traceback = debug.traceback(coroutine.running(),
                                     self.message_str, level + 3)
end

function WrappedError:__tostring()
    return self.message_str
end

local function wrap(e)
    if WrappedError:class_of(e) then
        return e
    else
        return WrappedError(e, 2)
    end
end
M.wrap = wrap

local function unwrap(e)
    if WrappedError:class_of(e) then
        return e.message
    else
        return e
    end
end
M.unwrap = unwrap

local function incr_level(level, delta)
    level = level or 1
    delta = delta or 1
    if level ~= 0 then
        level = level + delta
    end
    return level
end

local function create(e, nested_error, level)
    level = level or 1
    if not WrappedError:class_of(e) then
        if level ~= 0 then
            -- error() adorns strings with information about where the
            -- error was generated. Do the same.
            if type(e) == 'number' or type(e) == 'string' then
                local info = debug.getinfo(level + 1)
                if info.what == 'Lua' then
                    e = string.format(
                        '%s:%d: %s', info.short_src, info.currentline, e)
                end
            end
        else
            level = 1
        end
        e = WrappedError(e, level + 1)  -- strip create()
    end
    e.nested_error = nested_error
    return e
end
M.create = create

local function throw(e, nested_error, level)
    -- strip throw()
    error(create(e, nested_error, incr_level(level)), 0)
end
M.throw = throw

local function format(e)
    if WrappedError:class_of(e) then
        if e.nested_error then
            return string.format(
                '%s\nwhile processing nested error:\n%s',
                e.traceback,
                util.indent_lines(format(e.nested_error), '\t'))
        end
        return e.traceback
    end
    return safe_tostring(e)
end
M.format = format

local ok, thrift = pcall(require, 'fb.thrift')
if ok then
    thrift.add_penlight_classes(
        {WrappedError = WrappedError},
        'fb.util.error')
end

return M
