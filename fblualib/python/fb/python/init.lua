--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

local pl = require('pl.import_into')()
local lib = require('fb.python.lib')

local M = pl.tablex.copy(lib)

-- Retrieve vars visible at a given level of the stack.
-- Only returns vars that are convertible to Python, and no vars that start
-- with _ (so we don't pollute the Python namespace with our private variables)
local function get_vars(level)
    level = level + 1
    local vars = {}

    local modules = {}
    for _, v in pairs(package.loaded) do
        modules[v] = true
    end

    local function add(name, value)
        local first = name:sub(1, 1)
        -- '(': internal variable (from debug.getlocal or debug.getupvalue)
        -- '_': likely private, let's not pollute the namespace
        if first == '(' or first == '_' or vars[name] then
            return
        end
        local ty = type(value)
        if ty == 'function' or ty == 'cdata' then
            return
        end
        -- exclude modules
        if modules[value] then
            return
        end

        vars[name] = value
    end

    -- locals
    local i = 1
    while true do
        local name, value = debug.getlocal(level, i)
        if not name then
            break
        end
        add(name, value)
        i = i + 1
    end

    local info = debug.getinfo(level, 'f')
    if info.func then
        -- upvalues
        i = 1
        while true do
            local name, value = debug.getupvalue(info.func, i)
            if not name then
                break
            end
            add(name, value)
            i = i + 1
        end
        -- globals
        for name, value in pairs(debug.getfenv(info.func)) do
            add(name, value)
        end
    end

    return vars
end
M.get_vars = get_vars

-- Execute Python code in context of current stack frame
local function wrap(name)
    local function wrapped(code, level)
        if not level then
            level = 1
        end
        leve = level + 1
        return lib[name](code, get_vars(level), true)
    end
    M['l' .. name] = wrapped
end

wrap('exec')
wrap('eval')
wrap('reval')

return M
