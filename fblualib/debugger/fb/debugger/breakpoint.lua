--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

local pl = require('pl.import_into')()
local printf = pl.utils.printf
local utils = require('fb.debugger.utils')
local ml = require('fb.util.multi_level')
local plural = utils.plural

local M = {}

local next_breakpoint = 1
local breakpoints = { }
local file_breakpoints = { }
local function_breakpoints = { }

local Breakpoint = pl.class()
M.Breakpoint = Breakpoint

function Breakpoint:_init()
    self.enabled = true
    self.hit_count = 0
end

function Breakpoint:hit()
    self.hit_count = self.hit_count + 1
    if not self.quiet then
        printf('Hit %s breakpoint %d at %s (total %s %s)\n',
               self:type(), self.id, self:str(), self.hit_count,
               plural(self.hit_count, 'time'))
    end
    if self.hit_action then
        self.hit_action()
    end
end

function Breakpoint:describe()
    local enabled_str = 'n'
    if self.enabled then
        enabled_str = 'y'
    end
    return string.format('%-d\t%s\t%s   %s', self.id, self:type(), enabled_str,
                         self:str())
end

function Breakpoint:matches(s)
    return self.enabled and self:_matches(s)
end

function Breakpoint:register()
    assert(not self.id)
    self.id = next_breakpoint
    next_breakpoint = next_breakpoint + 1
    assert(not breakpoints[self.id])
    breakpoints[self.id] = self
    return self.id
end

function Breakpoint:unregister()
    assert(self.id)
    assert(breakpoints[self.id] == self)
    breakpoints[self.id] = nil
    self.id = nil
end

local FileBreakpoint = pl.class(Breakpoint)
M.FileBreakpoint = FileBreakpoint

function FileBreakpoint:_init(location, line)
    self:super()
    if string.sub(location, 1, 1) ~= '@' then
        error(string.format("Location '%s' is not a file", location))
    end
    self.file = string.sub(location, 2)
    self.line = tonumber(line)
    if not self.line then
        error(string.format("Line '%s' is not a number", line))
    end
end

function FileBreakpoint:_matches(info)
    if info.currentline ~= self.line or
       string.sub(info.source, 1, 1) ~= '@' then
        return false
    end
    local source_file = string.sub(info.source, 2)
    return source_file == self.file or
           pl.stringx.endswith(source_file, '/' .. self.file)
end

function FileBreakpoint:str()
    return string.format('%s:%d', self.file, self.line)
end

function FileBreakpoint:type()
    return 'file'
end

function FileBreakpoint:register()
    local bps = ml.setdefault(file_breakpoints, self.line, self.file, {})
    if next(bps) then
        printf('Warning: %d %s already set at %s\n ',
               #bps, plural(#bps, 'breakpoint'), self:str())
    end
    table.insert(bps, self)
    self._base.register(self)
    if not self.quiet then
        printf('File breakpoint %d set as %s\n', self.id, self:str())
    end
end

function FileBreakpoint:unregister()
    local bps = ml.get(file_breakpoints, self.line, self.file)
    assert(bps)
    local idx = pl.tablex.find(bps, self)
    assert(idx)
    table.remove(bps, idx)
    if not next(bps) then
        ml.del(file_breakpoints, self.line, self.file)
    end
    self._base.unregister(self)
end

local FunctionBreakpoint = pl.class(Breakpoint)
M.FunctionBreakpoint = FunctionBreakpoint

function FunctionBreakpoint:_init(func, line)
    self:super()
    if type(func) ~= 'function' then
        error(string.format("'%s' is not a function: %s",
                            func, type(func)))
    end
    self.func = func
    if line then
        self.line = tonumber(line)
        if not self.line then
            error(string.format("Line '%s' is not a number", line))
        end
    end
    local info = debug.getinfo(func, 'nlS')
    assert(info)
    self.name = info.name
    self.source = info.source
    self.linedefined = info.linedefined
end

function FunctionBreakpoint:_matches(info)
    if info.func ~= self.func then
        return false
    end
    if self.line and info.currentline ~= self.line then
        return false
    end
    return true
end

function FunctionBreakpoint:str()
    local msg = tostring(self.func)
    if self.source then
        msg = msg .. string.format(' %s', self.source)
    end
    local line = self.line or self.linedefined
    if line then
        msg = msg .. string.format(':%d', line)
    end
    return msg
end

function FunctionBreakpoint:type()
    return 'func'
end

function FunctionBreakpoint:register()
    local bps = ml.setdefault(function_breakpoints, self.func, {})
    if next(bps) then
        printf('Warning: %d %s already set at %s\n',
               #bps, plural(#bps, 'breakpoint'), self:str())
    end
    table.insert(bps, self)
    self._base.register(self)
    if not self.quiet then
        printf('Function breakpoint %d set at %s\n', self.id, self:str())
    end
end

function FunctionBreakpoint:unregister()
    local bps = ml.get(function_breakpoints, self.func)
    assert(bps)
    local idx = pl.tablex.find(bps, self)
    assert(idx)
    table.remove(bps, idx)
    if not next(bps) then
        ml.del(function_breakpoints, self.func)
    end
    self._base.unregister(self)
end

function M.has_file_breakpoints()
    return next(file_breakpoints)
end

function M.has_breakpoints_at_line(line)
    return ml.get(file_breakpoints, line)
end

function M.get_file_breakpoints(file, line)
    return ml.get(file_breakpoints, line, file)
end

function M.has_function_breakpoints()
    return next(function_breakpoints)
end

function M.get_function_breakpoints(func)
    return ml.get(function_breakpoints, func)
end

function M.list_breakpoints()
    print('Num\tType\tEnb Location')
    for _,b in pl.tablex.sort(breakpoints) do
        print(b:describe())
    end
end

function M.get(id)
    return breakpoints[id]
end

return M
