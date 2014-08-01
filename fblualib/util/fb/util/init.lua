--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

-- Various utilities that don't fit anywhere else.
--
-- Data structures:
-- * Deque, a deque implementation
-- * set_default, modify a table to always return (and optionally store)
--   a default value
--
-- String utilities:
-- * longest_common_prefix, return the longest common prefix of multiple
--   strings
--
-- Filesystem utilities:
-- * create_temp_dir, create a temporary directory
--
-- Clock utilities:
-- * time(), return the time (as a floating-point number of seconds since
--   epoch); just like os.time() but with microsecond precision.
-- * monotonic_clock(), return the time (as a floating-point number of seconds
--   since an arbitrary time); may be faster than time(), and won't go
--   backwards; used for measuring intervals.
-- * sleep(microseconds), sleep for a number of seconds
--
-- Math utilities:
-- * random_seed(), return a "good" random number seed; do not use for
--   crypto. (But WTF, you're not doing crypto in Lua anyway, are you?)
--
-- Torch tensor utilities:
-- * find(), given a 1d byte tensor, return a 1d long tensor containing the
--   (1-based) indices of non-zero values in the input

local ffi = require('ffi')
local pl = require('pl.import_into')()
local ok, torch = pcall(require, 'torch')
if not ok then torch = nil end
local module_config = require('fb.util._config')

local M = {}

-- Deque. Supports the usual (push|pop)_(front|back) operations.
--
-- Supports random access, either by the usual 1-based index, or by a
-- stable index that doesn't change when elements are added or removed.
--
-- The stable indexes for the front and back of the queue are given by
-- deque.first and deque.last. Use get_stable and set_stable to access by
-- stable index.
--
-- Implementation is a bit tricky because we want to make deque[1] be the
-- first element in the deque, but we store elements in a table keyed
-- by stable indexes. So __index and __newindex have to offset the numeric
-- indices, and they're also used to look up method names in the class object.
local Deque = {}

function Deque:_init(lst)
    lst = lst or {}
    if getmetatable(lst) == Deque then
        self.list = pl.tablex.copy(lst.list)
        self.first = lst.first
        self.last = lst.last
    else
        assert(type(lst) == 'table')
        self.list = pl.tablex.copy(lst)
        self.first = 1
        self.last = #lst
    end
end

function Deque:_iter(idx)
    if idx and idx >= self.first - 1 and idx < self.last then
        idx = idx + 1
        return idx, self.list[idx]
    end
end

-- Iterator
function Deque:__ipairs()
    return self._iter, self, 0
end

-- Iterator
function Deque:__pairs()
    return self.__ipairs()
end

-- Deque length
function Deque:__len()
    return self.last - self.first + 1
end

function Deque:push_back(elem)
    self.last = self.last + 1
    self.list[self.last] = elem
    return self
end

function Deque:push_front(elem)
    self.first = self.first - 1
    self.list[self.first] = elem
    return self
end

-- Pop n elements (default 1) from the back, return the last element popped
function Deque:pop_back(n)
    n = n or 1
    assert(self.last - self.first >= n - 1)
    local r = self.list[self.last - n + 1]
    for i = 1, n do
        self.list[self.last - i + 1] = nil
    end
    self.last = self.last - n
    return r
end

-- Pop n elements (default 1) from the front, return the last element popped
function Deque:pop_front(n)
    n = n or 1
    assert(self.last - self.first >= n - 1)
    local r = self.list[self.first + n - 1]
    for i = 1, n do
        self.list[self.first + i - 1] = nil
    end
    self.first = self.first + n
    return r
end

-- Get by stable index
function Deque:get_stable(idx)
    assert(idx >= self.first and idx <= self.last)
    return self.list[idx]
end

-- Set by stable index
function Deque:set_stable(idx, value)
    assert(idx >= self.first and idx <= self.last)
    self.list[idx] = value
end

-- Clear the deque; invalidates stable indexes.
function Deque:clear()
    self.list = {}
    self.first = 1
    self.last = 0
end

-- Comparison
function Deque:__eq(other)
    if #self ~= #other then
        return false
    end
    for i = 1, #self do
        if self[i] ~= other[i] then
            return false
        end
    end
    return true
end

-- Lookup
function Deque:__index(idx, ...)
    if type(idx) == 'string' then  -- method lookup
        return rawget(Deque, idx)
    end
    -- element lookup
    assert(idx >= 1 and idx <= #self)
    return self.list[idx + self.first - 1]
end

function Deque:__newindex(idx, value)
    -- element lookup
    assert(idx >= 1 and idx <= #self)
    self.list[idx + self.first - 1] = value
end

function Deque:__call(...)
    assert(self == Deque)
    local a = {...}
    local obj = {}
    -- Do this before setmetatable, so that initialization (self.first, etc)
    -- doesn't go through __newindex
    Deque._init(obj, ...)
    setmetatable(obj, Deque)
    return obj
end

setmetatable(Deque, Deque)
M.Deque = Deque

-- Set a callback that is called to obtain the default value for a table;
-- similar to Python's collections.defaultdict. (It must be a callback so you
-- create a new instance every time; you can't set the default value to {}, or
-- all elements will default to the *same* object!)
-- If store is true, then a lookup for an element that doesn't exist
-- will store the default value in the table.
local function set_default(table, default_cb, store)
    assert(not getmetatable(table))
    assert(type(default_cb) == 'function')
    setmetatable(table, {
        __index = function(t, k)
            local v = default_cb()
            if store then t[k] = v end
            return v
        end,
    })
end
M.set_default = set_default

-- Determine the longest prefix among a list of strings
local function longest_common_prefix(strings)
    if #strings == 0 then
        return ''
    end
    local prefix = strings[1]
    for i = 2, #strings do
        local s = strings[i]
        local len = 0
        for j = 1, math.min(#s, #prefix) do
            if s:sub(j, j) == prefix:sub(j, j) then
                len = len + 1
            else
                break
            end
        end
        prefix = prefix:sub(1, len)
        if len == 0 then
            break
        end
    end
    return prefix
end
M.longest_common_prefix = longest_common_prefix

ffi.cdef([=[
char* mkdtemp(char*);
]=])

-- Create a temporary directory; prefix is the prefix of the directory name
-- (a random suffix will be added). If prefix is relative, we'll create the
-- directory starting from a system-specific location ($TMPDIR or /tmp).
local function create_temp_dir(prefix)
    prefix = prefix or 'lua_temp'
    if not pl.path.isabs(prefix) then
        local tmpdir = os.getenv('TMPDIR') or '/tmp'
        prefix = pl.path.join(tmpdir, prefix)
    end
    local path = prefix .. '.XXXXXX'
    local buf = ffi.new('char[?]', #path + 1)
    ffi.copy(buf, path)
    local ret = ffi.C.mkdtemp(buf)
    if not ret then
        local errno = ffi.errno()
        error(string.format('mkdtemp failed: %d', errno))
    end
    return ffi.string(ret)
end
M.create_temp_dir = create_temp_dir

local util_lib = ffi.load(module_config.clib)
ffi.cdef([=[
int64_t getMicrosecondsMonotonic();
int64_t getMicrosecondsRealtime();
void sleepMicroseconds(int64_t);
uint32_t randomNumberSeed();
]=])

local function time()
    return tonumber(util_lib.getMicrosecondsRealtime()) / 1e6
end
M.time = time

local function monotonic_clock()
    return tonumber(util_lib.getMicrosecondsMonotonic()) / 1e6
end
M.monotonic_clock = monotonic_clock

local function sleep(s)
    assert(s >= 0)
    util_lib.sleepMicroseconds(s * 1e6)
end
M.sleep = sleep

local function random_seed()
    return tonumber(util_lib.randomNumberSeed())
end
M.random_seed = random_seed

if torch then
    M.find = function(byte_input)
        if byte_input:type()  ~= "torch.ByteTensor" then
            error("input should be 1d torch byte tensor")
        end
        if byte_input:dim() ~= 1 then
            error("input should be 1d torch byte tensor")
        end

        local N = byte_input:size(1)
        local stride = byte_input:stride(1)
        local iptr = byte_input:data()
        local offset = 0

        local output=torch.zeros(N):long()
        local optr = output:data()
        local j = 0
        for i = 0, N-1 do
            if iptr[offset] ~= 0 then
                optr[j] = i + 1
                j = j + 1
            end
            offset = offset + stride
        end

        return output:sub(1, j)
    end
end

return M
