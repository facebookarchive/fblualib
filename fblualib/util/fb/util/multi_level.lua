--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

-- utilities to deal with multi-level tables (tables of tables)
--
-- set(table, k1, k2, ..., kn, val) is the same as
--   table[k1][k2]...[kn] = val
-- except that intermediate tables are created automatically
--
-- del(table, k1, k2, ..., kn) is the same as
--   table[k1][k2]...[kn] = nil
-- except that intermediate tables that have become empty are deleted
--
-- get(table, k1, k2, ..., kn) is the same as
--   table[k1][k2]...[kn]
-- except that it returns nil if any intermediate table is missing
-- (rather than throwing an error)
local function del(t, ...)
    local args = {...}
    local stack = { }
    local lt
    local nlt = t
    for i = 1, #args do
        lt = nlt
        if type(lt) ~= 'table' then
            return false
        end
        stack[i] = lt
        nlt = lt[args[i]]
        if nlt == nil then
            return false
        end
    end
    lt[args[#args]] = nil

    local i = #args
    while i >= 1 and not next(stack[i]) do
        i = i - 1
    end
    if i >= 1 then
        stack[i][args[i]] = nil
    end
    return true
end

local function _set(overwrite, t, ...)
    local args = {...}
    local lt = t
    for i = 1, #args - 2 do
        local nlt = lt[args[i]]
        if not nlt then
            nlt = { }
            lt[args[i]] = nlt
        end
        lt = nlt
    end
    if not overwrite then
        local prev = lt[args[#args - 1]]
        if prev ~= nil then
            return prev
        end
    end
    local v = args[#args]
    lt[args[#args - 1]] = v
    return v
end

local function set(t, ...)
    _set(true, t, ...)
end

local function setdefault(t, ...)
    return _set(false, t, ...)
end

local function get(t, ...)
    local args = {...}
    local lt
    local nlt = t
    for i = 1, #args do
        lt = nlt
        if type(lt) ~= 'table' then
            return nil
        end
        nlt = lt[args[i]]
        if nlt == nil then
            return false
        end
    end
    return nlt
end

return {
    get = get,
    set = set,
    setdefault = setdefault,
    del = del,
}
