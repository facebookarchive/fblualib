--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

-- Tracing facility.
--
-- This module implements a hierarchical tracing facility.
--
-- At the core, the function trace() allows you to fire a trace event
-- ("probe"), which is a {key, args} tuple.
--
-- The key is made up of 4 components in decreasing order of signficance:
-- DOMAIN, MODULE, FUNCTION, and EVENT, separated by colon (':'). The meaning
-- of the four components is somewhat arbitrary:
--
-- - DOMAIN specifies the high-level activity that your program is undertaking
--   (for example, if your program is training multiple neural nets in
--   sequence, this could be the name of the net being trained)
--
-- - MODULE identifies the library / module,
--
-- - FUNCTION identifies the location in the code (usually the name of a
--   function, appropriately qualified to distinguish it in the library)
--
-- - EVENT indicates the event within a function (arbitrary name, although
--   "entry" and "return" are used to signify entry and return to/from the
--   function, respectively; see trace_function() below)
--
-- args is an arbitrary tuple (although some probes have specific meanings for
-- args, see below).
--
-- The tracing facility sets args.time to the current time (as a floating point
-- number of seconds since Epoch).
--
-- namespace() will set the default first components of the key for the
-- duration of its callback; usually, this is used to set the DOMAIN around
-- calls into a module, and the MODULE within a module.
--
-- trace_function() wraps a function execution and fires two events, with the
-- EVENTs "entry" and "return".  "entry" sets args.args to the list of
-- arguments passed to the function; "return" sets args.ret to the list of
-- values returned by the function, or args.error to the error string if the
-- function threw an error. trace_function also tracks function probe nesting
-- level (which can be used for prettily indented output).
--
-- Callers may register "handlers" that will be called when certain probes
-- (matching specified patterns) fire.

local pl = require('pl.import_into')()
local printf = pl.utils.printf
local util = require('fb.util')
local eh = require('fb.util.error')

local M = {}

local DOMAIN, MODULE, FUNCTION, EVENT = unpack(pl.tablex.range(1, 4))
local PREFIX = -EVENT
local PATTERN = PREFIX

local ENTRY = ':::entry'
local RETURN = ':::return'

M.DOMAIN = DOMAIN
M.MODULE = MODULE
M.FUNCTION = FUNCTION
M.EVENT = EVENT
M.PREFIX = PREFIX
M.PATTERN = PATTERN
M.ENTRY = ENTRY
M.RETURN = RETURN

-- Define a high-resolution

-- Export the clock we use
local clock = util.time
M.clock = clock

-- Return true iff a key matches a pattern (both given as tuples)
local function key_matches(key, pattern)
    pattern = pattern or {}
    for i = 1, #pattern do
        if not key[i]:match(string.format('^%s$', pattern[i])) then
            return false
        end
    end
    return true
end
M.key_matches = key_matches

-- Format a key (convert from tuple back to string)
local function format_key(key)
    return table.concat(key, ':')
end
M.format_key = format_key

-- Matcher class. Associate handlers with patterns.
local Matcher = pl.class()

function Matcher:_init()
    self.patterns = {}
    util.set_default(self.patterns, function() return {} end, true)
end

-- Add a pattern for the given handler (which is added to our set if
-- it does not exist).
--
-- The pattern is a string of up to 4 components that all keys are matched
-- against. Each component is a Lua pattern that must match fully. If a
-- pattern component fails to match, the pattern fails. If all pattern
-- components match, the pattern matches. (Note that further components in
-- the key are ignored)
--
-- Examples:
--
-- 'foo:bar'    matches foo:bar:x:y, but not foo:bars:x:y
-- 'foo:bar:x'  matches foo:bar:x:y, but not foo:bar:y:z
-- 'foo:ba.'    matches foo:bar:x:y, foo:baz:x:y, but not foo:bars:x:y
-- 'foo:ba.*'   matches foo:bar:x:y, foo:bars:x:y, but not foo:qux:x:y
-- 'foo:ba.*:x' matches foo:bar:x:y, but not foo:bar:y:z
--
-- Matching stops at the first matching pattern. If that is a pass pattern
-- (fail is false), we call the handler. If it's a fail pattern (fail is true),
-- we don't. If no pattern matches, we fail (don't call the handler) by
-- default, but you can change that by adding a match-all pattern at the end:
-- add(handler, '')
function Matcher:add(handler, pattern, fail)
    table.insert(self.patterns[handler], {pattern, fail})
end

-- Remove a handler.
function Matcher:remove(handler)
    self.patterns[handler] = nil
end

-- Match a key against all patterns, call all handlers that match.
function Matcher:match(key, ...)
    for handler, patterns in pairs(self.patterns) do
        for _, p in ipairs(patterns) do
            local pattern, fail = unpack(p, 1, 2)
            if key_matches(key, pattern) then
                if not fail then
                    handler(key, ...)
                end
                break
            end
        end
    end
end

-- Class representing the state of the tracing subsystem
local TraceState = pl.class()

function TraceState:_init()
    self.default_key = pl.List()
    for i = 1, EVENT do
        self.default_key:append('')
    end
    self.matcher = Matcher()
    self.nesting_level = 0
end

-- Parse a key into a tuple.
-- The key is absolute, but empty components are inherited from the default
-- (set with namespace()).
--
-- n is the key level (number of component expected); if n >= 0, we assert
-- that the key has exactly n components; if n < 0, we assert that the
-- key has <= -n components.
function TraceState:parse_key(key_str, n)
    local parts = pl.List(pl.stringx.split(key_str, ':'))
    if not n then
        n = EVENT
    end
    if n < 0 then
        assert(#parts <= -n)
    else
        assert(#parts == n)
    end
    for i = 1, #parts do
        if parts[i] == '' then
            parts[i] = self.default_key[i]
        else
            break
        end
    end
    return parts
end

-- Wrap some code and set default key components
-- Example:
--
-- namespace('imagenet:train', fprop)
--
-- function fprop()
--   trace('::fprop:start', ...)  -- fires imagenet.train.fprop.start
function TraceState:namespace(key_str, closure, ...)
    local old_default_key = self.default_key
    self.default_key = self:parse_key(key_str, PREFIX)
    return eh.finally(
        closure,
        function() self.default_key = old_default_key end,
        ...)
end

function TraceState:_trace(key, args, nest)
    nest = nest or 0
    if nest > 0 then
        self.nesting_level = self.nesting_level + nest
    end
    args.time = clock()
    self.matcher:match(key, args, self.nesting_level)
    if nest < 0 then
        self.nesting_level = self.nesting_level + nest
    end
end

-- Trace a function call. Generate 'entry' and 'return' events.
--
-- trace_function('domain.module.func', func, 42, 'meow')
--   fires: domain.module.func.entry, {args = {42, 'meow'}, time = ...}
--   ...
--   fires: domain.module.func.return, {ret = {<return_values>}, time = ...}
--     or   domain.module.func.return, {error = <error_str>, time = ...}
function TraceState:trace_function(key_str, func, ...)
    return self:namespace(key_str, self._trace_function, self, func, ...)
end

function TraceState:_trace_function(func, ...)
    self:_trace(self:parse_key(ENTRY), {func = func, args = {...}}, 1)
    local ret = {eh.on_error(
        func,
        function(err)
            self:_trace(self:parse_key(RETURN), {error = err}, -1)
        end,
        ...)}
    self:_trace(self:parse_key(RETURN), {ret = ret}, -1)
    return unpack(ret)
end

-- Trace an event.
function TraceState:trace(key, args)
    return self:_trace(self:parse_key(key), args or {})
end

-- Add a probe handler; see the documentation in Matcher:add()
function TraceState:add_handler(handler, pattern_str, fail)
    local pattern = self:parse_key(pattern_str, PREFIX)
    self.matcher:add(handler, pattern, fail)
end

-- Remove a probe handler; see the documentation in Matcher:remove()
function TraceState:remove_handler(handler)
    self.matcher:remove(handler)
end

local trace_state = TraceState()

for _, fn in ipairs({'parse_key', 'namespace', 'trace', 'trace_function',
                     'add_handler', 'remove_handler'}) do
    M[fn] = pl.func.bind1(trace_state[fn], trace_state)
end

return M
