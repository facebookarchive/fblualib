--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

-- Trace timing.
--
-- This modules implements a handler for the tracing facility that
-- logs the probe key, time, and nesting level for all probes matching
-- certain patterns. It can then print the log (with timing information)
-- prettily to standard output.

local pl = require('pl.import_into')()
local printf = pl.utils.printf

local util = require('fb.util')
local trace = require('fb.util.trace')

local M = {}

local Recorder = pl.class()

local timing_module = 'fb:timing'

function Recorder:_init(max_size)
    self.events = util.Deque()
    self.max_size = max_size
    self.registered_patterns = pl.List()
    self.patterns = pl.List()
    self.enabled = false
    self.record_func = pl.func.bind1(self.record, self)
end

function Recorder:_enable()
    -- always listen to our own events
    trace.add_handler(self.record_func, timing_module)
    for _, p in ipairs(self.patterns) do
        local pattern, fail = unpack(p, 1, 2)
        trace.add_handler(self.record_func, pattern, fail)
    end
    self.registered_patterns:extend(self.patterns)
    self.patterns = {}
    self.enabled = true
end

function Recorder:_disable()
    trace.remove_handler(self.record_func)
    self.registered_patterns:extend(self.patterns)
    self.patterns = self.registered_patterns
    self.registered_patterns = {}
    self.enabled = false
end

-- Record an event, used as handler
function Recorder:record(key, args, nest)
    if self.enabled then
        self.events:push_back({key, args.time, nest})
        self:set_max_size()
    end
end

-- Set max size (if max_size is not nil) and ensure that the current size
-- is under max_size.
function Recorder:set_max_size(max_size)
    if max_size then
        self.max_size = max_size
    end
    if self.max_size and #self.events > self.max_size then
        self.events:pop_front(#self.events - self.max_size)
    end
end

-- Return an integer indicating the current position in the log.
-- This can be passed to dump() to indicate a starting position.
function Recorder:mark()
    return self.events.last + 1
end

-- Clear all events, reset the start time, reinitialize, enable
function Recorder:start(name)
    self:reset()
    self.name = (name or 'default_path'):gsub('[:%s]', '_')
    return self:resume()
end

-- Resume recording after being stopped. Current timing information is
-- kept (and the time between stop() and resume() will be accounted for
-- in the output); use start() to restart from scratch.
function Recorder:resume()
    self:_enable()
    trace.trace(string.format('%s:start:%s', timing_module, self.name))
    return self:mark()
end

-- Stop recording.
function Recorder:stop()
    if self.enabled then
        trace.trace(string.format('%s:stop:%s', timing_module, self.name))
        self:_disable()
    end
end

-- Finish; this is a convenience function that calls stop() and dumps
-- the output; the arguments are the same as for dump().
function Recorder:finish(...)
    self:stop()
    self:dump(...)
    self:reset()
end

-- Dump the log to stdout.
-- If pattern_str is not nil, only the records matching the pattern will
-- be dumped. If start is not nil, only the records after the requested
-- start position (returned by mark()) will be dumped.
function Recorder:dump(pattern_str, start)
    if not self.name then return end
    pattern_str = pattern_str or ''
    if start then
        start = math.max(start, self.events.first)
    else
        start = self.events.first
    end

    local pattern = trace.parse_key(pattern_str, trace.PATTERN)
    local start_time, last_time
    printf('%-62.62s %8s %8s\n', self.name, 'pt', 'cum')

    for i = start, self.events.last do
        local event = self.events:get_stable(i)
        local key, time, nest = unpack(event)
        if pattern_str == '' or
           trace.key_matches(key, pattern) or
           trace.key_matches(key, timing_module) then
            if not start_time then
                start_time = time
                last_time = time
            end
            local indent = string.rep('  ', nest)
            printf('%-62.62s %8d %8d\n',
                   indent .. trace.format_key(key),
                   (time - last_time) * 1e6,
                   (time - start_time) * 1e6)
            last_time = time
        end
    end
end

-- Reset the recorder to a pristine state.
function Recorder:reset()
    self:stop()
    self.events:clear()
    self.name = nil
    self.patterns = pl.List()
    self.registered_patterns = pl.List()
end

-- Add a pattern (Lua string pattern matched against the string key)
function Recorder:add_pattern(pattern_str, fail)
    table.insert(self.patterns, {pattern_str, fail})
    if self.enabled then
        -- enable() always updates the registration
        self:_enable()
    end
end

local recorder = Recorder()

-- Add recording for a pattern
for _, fn in ipairs({'set_max_size', 'mark', 'reset', 'dump',
                     'start', 'stop', 'resume', 'reset', 'finish',
                     'add_pattern'}) do
    M[fn] = pl.func.bind1(recorder[fn], recorder)
end

return M
