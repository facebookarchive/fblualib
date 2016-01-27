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
-- * defaultdict, return a table modified to return (and store) a default value
--   for missing keys, similar to Python's collections.defauldict
--
-- String utilities:
-- * longest_common_prefix, return the longest common prefix of multiple
--   strings
-- * std_string, a Lua string-like FFI type that wraps std::string; useful
--   when writing FFI Lua code that interfaces with C++ code.
--   Supports #str, (mutable) str:append(...), str:clear(), str:sub(),
--   tostring(str), str1 .. str2
-- * c_escape / c_unescape, escape / unescape special characters that couldn't
--   go inside a C string (see folly::cEscape in <folly/String.h>)
-- * splitlines(text, keepends), split a string into lines, as per
--   https://docs.python.org/2/library/stdtypes.html#str.splitlines
--   (Note that Penlight's version is incorrect)
-- * indent_lines(text, indent), prepend indent to every line in the given
--   text
-- * utf8_get, retrieve one UTF-8 character at the given string position
-- * utf8_iter, iterate over strings as sequences of UTF-8 characters
--
-- Table utilities:
-- * filter_keys: given a table and a list of keys, return a subset of the table
--   consisting of the elements whose keys are in the list of keys
-- * is_list: check if a table is list-like (all keys are consecutive
--   integers starting at 1). O(N) in the size of the table.
-- * pack_n: pack varargs into a list-like table, also setting the special
--   member 'n' to indicate the number of values (some might be holes); similar
--   to _G.arg
-- * unpack_n: the reverse of pack_n, expand the values in a list-like table
--   with a 'n' member into multiple return values
--
-- Filesystem utilities:
-- * create_temp_dir, create a temporary directory
--
-- Multithreading utilities:
-- * get_once, get or create an once_record object (by unique string name)
-- * pcall_once, ensure that a function is called successfully no more than
--   once in the same process, among calls with the same once_record object
-- * get_mutex, get or create a mutex object (by unique string name)
-- * pcall_locked / call_locked, ensure mutual exclusion among concurrent calls
--   with the same mutex object
--
-- Module utilities:
-- * current_module, return the current module load path (argument to
--   "require" that would load the current module) and a "is_package" argument
--   which is true if we are at "package level" (in a file named init.lua)
-- * import, equivalent to Python's relative imports; the semantics are
--   similar to the built-in function require(), except that, if the load path
--   starts with ".", it is computed relative to the current module
--   (import('.foo') loads modules from the same directory, import('..foo')
--    loads modules from the parent directory, etc)
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
--
-- System utilities:
-- * setenv(), wrapper around the setenv library call
-- * unsetenv(), wrapper around the unsetenv library call

local ffi = require('ffi')
local pl = require('pl.import_into')()
local ok, torch = pcall(require, 'torch')
if not ok then torch = nil end
local bit = require('bit')

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
    if idx and idx >= 0 and idx < #self then
        idx = idx + 1
        return idx, self.list[idx + self.first - 1]
    end
end

-- Iterator
function Deque:__ipairs()
    return self._iter, self, 0
end

-- Iterator
function Deque:__pairs()
    return self:__ipairs()
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
function Deque:__index(idx)
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

local function new_deque(...)
    local obj = {}
    -- Do this before setmetatable, so that initialization (self.first, etc)
    -- doesn't go through __newindex
    Deque._init(obj, ...)
    setmetatable(obj, Deque)
    return obj
end

setmetatable(Deque, {__call = new_deque})

M.Deque = Deque


-- Set a callback that is called to obtain the default value for a table;
-- similar to Python's collections.defaultdict. (It must be a callback so you
-- create a new instance every time; you can't set the default value to {}, or
-- all elements will default to the *same* object!)
--
-- If store is true, then a lookup for an element that doesn't exist
-- will store the default value in the table.
--
-- mode is the weakness mode as in the __mode field in the metatable
-- (nil, 'k', 'v', 'kv')
local function set_default(table, default_cb, store, mode)
    assert(not getmetatable(table))
    assert(type(default_cb) == 'function')
    setmetatable(table, {
        __index = function(t, k)
            local v = default_cb()
            if store then t[k] = v end
            return v
        end,
        __mode = mode,
    })
end
M.set_default = set_default


-- table with customizable factory for constructing default values
-- for keys that do not exist.
local function defaultdict(default_value_factory)
    local d = {}
    set_default(d, default_value_factory, true)
    return d
end
M.defaultdict = defaultdict


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


local lib_path = package.searchpath('libfbutil', package.cpath)
if not lib_path then
   lib_path = require('fb.util._config').clib
end
local util_lib = ffi.load(lib_path)
M._clib = util_lib

ffi.cdef([=[
int64_t getMicrosecondsMonotonic();
int64_t getMicrosecondsRealtime();
void sleepMicroseconds(int64_t);
uint32_t randomNumberSeed();
]=])


-- Return time as a floating-point number of seconds since Epoch
local function time()
    return tonumber(util_lib.getMicrosecondsRealtime()) / 1e6
end
M.time = time


-- Return time as a floating-point number of seconds since an unspecified
-- time; never goes backwards -- use for measuring intervals.
local function monotonic_clock()
    return tonumber(util_lib.getMicrosecondsMonotonic()) / 1e6
end
M.monotonic_clock = monotonic_clock


-- Sleep for a floating point number of seconds
local function sleep(s)
    assert(s >= 0)
    util_lib.sleepMicroseconds(s * 1e6)
end
M.sleep = sleep


-- Return a "good" random number seed
local function random_seed()
    return tonumber(util_lib.randomNumberSeed())
end
M.random_seed = random_seed


ffi.cdef([=[
void* stdStringNew();
void* stdStringNewFromString(const char*, size_t);
void* stdStringClone(const void*);
void stdStringDelete(void*);
void stdStringClear(void*);
bool stdStringAppend(void*, const char*, size_t);
bool stdStringAppendS(void*, const void*);
bool stdStringInsert(void*, size_t, const char*, size_t);
bool stdStringInsertS(void*, size_t, const void*);
bool stdStringReplace(void*, size_t, size_t, const char*, size_t);
bool stdStringReplaceS(void*, size_t, size_t, const void*);
void stdStringErase(void*, size_t, size_t);
const char* stdStringData(const void*);
size_t stdStringSize(const void*);

const char* cEscape(const char*, size_t, void*);
const char* cUnescape(const char*, size_t, void*);
]=])


-- FFI class that wraps st::string
local std_string = ffi.typeof('struct { void* _p; }')
local nullptr = ffi.new('void*')
local methods = {}

-- Clear the contents of the string
function methods:clear()
    util_lib.stdStringClear(self._p)
end

local function _string_pos(p, size)
    if p < 0 then
        p = p + size
    else
        p = p - 1
    end
    if p < 0 then
        p = 0
    end
    if p > size then
        p = size
    end
    return p
end

local function _string_pos_and_size(b, e, size)
    b = _string_pos(b, size)

    if not e then
        e = size
    elseif e < 0 then
        e = e + size + 1
    end

    if e > size then
        e = size
    end

    local n
    if e < b then
        n = 0
    else
        n = e - b
    end

    return b, n
end

-- Return a substring, same semantics as string:sub
function methods:sub(b, e)
    local data = util_lib.stdStringData(self._p)
    local pos, n = _string_pos_and_size(b, e, #self)

    return ffi.string(data + pos, n)
end

-- Insert a substring at a given position
function methods:insert(p, other)
    local pos = _string_pos(p, #self)
    local r
    if ffi.istype(std_string, other) then
        r = util_lib.stdStringInsertS(self._p, pos, other.p_)
    else
        local s = tostring(other)
        r = util_lib.stdStringInsert(self._p, pos, s, #s)
    end
    if not r then
        error('Out of memory')
    end
end

-- Replace a substring
function methods:replace(b, e, other)
    local pos, n = _string_pos_and_size(b, e, #self)
    local r
    if ffi.istype(std_string, other) then
        r = util_lib.stdStringReplaceS(self._p, pos, n, other.p_)
    else
        local s = tostring(other)
        r = util_lib.stdStringReplace(self._p, pos, n, s, #s)
    end
    if not r then
        error('Out of memory')
    end
end

-- Erase a substring
function methods:erase(b, e)
    local pos, n = _string_pos_and_size(b, e, #self)
    util_lib.stdStringErase(self._p, pos, n)
end

-- Append (mutating self) other to the end of self.
function methods:append(other)
    local r
    if ffi.istype(std_string, other) then
        r = util_lib.stdStringAppendS(self._p, other._p)
    else
        local s = tostring(other)
        r = util_lib.stdStringAppend(self._p, s, #s)
    end
    if not r then
        error('Out of memory')
    end
end

ffi.metatype(std_string, {
    __new = function(ct, s)
        local obj = ffi.new(ct)

        if s == nil then
            -- no arguments, create empty string
            obj._p = util_lib.stdStringNew()
        elseif ffi.istype(std_string, s) then
            -- clone std::string
            obj._p = util_lib.stdStringClone(s._p)
        else
            -- Lua string
            local str = tostring(s)
            obj._p = util_lib.stdStringNewFromString(str, #str)
        end

        if obj._p == nullptr then
            error('Out of memory')
        end

        return obj
    end,

    __gc = function(self)
        util_lib.stdStringDelete(self._p)
    end,

    __len = function(self)
        return tonumber(util_lib.stdStringSize(self._p))
    end,

    __tostring = function(self)
        return ffi.string(util_lib.stdStringData(self._p), #self)
    end,

    __concat = function(self, other)
        local res = std_string(self)  -- clone
        res:append(other)
        return res
    end,

    __index = methods,
})
M.std_string = std_string


local c_escape_buf = std_string()


-- C-Escape a string, making it suitable for representation as a C string
-- literal.
local function c_escape(str)
    c_escape_buf:clear()
    local err_msg = util_lib.cEscape(str, #str, c_escape_buf._p)
    if err_msg ~= nullptr then
        error(ffi.string(err_msg))
    end
    return tostring(c_escape_buf)
end
M.c_escape = c_escape


-- C-Unescape a string, the opposite of c_escape, above.
local function c_unescape(str)
    c_escape_buf:clear()
    local err_msg = util_lib.cUnescape(str, #str, c_escape_buf._p)
    if err_msg ~= nullptr then
        error(ffi.string(err_msg))
    end
    return tostring(c_escape_buf)
end
M.c_unescape = c_unescape

-- pl.stringx.splitlines doesn't allow you to keep line terminators,
-- and also incorrectly returns a single empty line when given an empty string.
-- This behaves like Python's str.splitlines.
local function splitlines(text, keepends)
    local delta = keepends and 0 or -1  -- keepends ? 0 : -1
    local lines = {}
    local start = 1
    while start <= #text do
        local pos = string.find(text, '\n', start, true)
        if not pos then
            table.insert(lines, string.sub(text, start))
            break
        end
        table.insert(lines, string.sub(text, start, pos + delta))
        start = pos + 1
    end

    return lines
end
M.splitlines = splitlines

local function indent_lines(text, indent)
    return table.concat(pl.tablex.imap(
        function(s) return indent .. s end,
        splitlines(text, true)))
end
M.indent_lines = indent_lines


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
        if j>0 then
            return output:sub(1, j)
        end
    end
end


-- Return a subset of "table" which contains only the elements whose keys
-- are in the list "keys"
local function filter_keys(table, keys)
    local result = {}
    for _, k in ipairs(keys) do
        local v = table[k]
        if v then
            result[k] = v
        end
    end
    return result
end
M.filter_keys = filter_keys


-- Return true if the table "table" is list-like (all its keys are consecutive
-- integers starting at 1); return false otherwise. O(N) in the size of the
-- table.
local function is_list(table)
    local n = 0
    local hash_size = #table
    for k, _ in pairs(table) do
        if type(k) ~= 'number' or k < 1 or k > hash_size then
            return false
        end
        n = n + 1
    end
    -- n <= hash_size by the pigeonhole principle
    return n == hash_size
end
M.is_list = is_list


local function pack_n(...)
    local r = {...}
    r.n = select('#', ...)
    return r
end
M.pack_n = pack_n

local function unpack_n(t, s, e)
    return table.unpack(t, s or 1, e or t.n or #t)
end
M.unpack_n = unpack_n

local function split_module_path(p)
    local parts = pl.stringx.split(p, '.')
    local is_init = false
    if parts[#parts] == 'init' then
        is_init = true
        table.remove(parts)
    end
    return parts, is_init
end

local function current_module_parts()
    -- Figure out the current module. This requires messing around with the
    -- debug API; we keep going up the stack (starting with the caller of
    -- current_module_parts()) until we find a chunk at module level.
    local level = 2
    local info
    while true do
        info = debug.getinfo(level, 'nS')
        if not info then
            error('Could not find current module')
        end
        if info.what == 'main' then
            break
        end
        level = level + 1
    end

    -- If this module wasn't loaded from a file (command-line, etc), bail out.
    if info.source:sub(1, 1) ~= '@' then
        error('Current module not loaded from a file: ' .. info.source)
    end

    -- If this module wasn't loaded from a Lua file, bail out.
    local r, ext = pl.path.splitext(pl.path.basename(info.source))
    if ext ~= '.lua' then
        error('Current module not loaded from a Lua file: ' .. info.source)
    end

    -- The module load path (fb.util.foo) is passed as the first vararg
    local vararg_name, path = debug.getlocal(level, -1)
    if not vararg_name then
        error('Module has empty load path: ' .. info.source)
    end

    -- This can still have false positives. You can load a chunk from a file
    -- f = loadfile('baz.lua') and then call it f('foo'), and then we'll find
    -- 'foo' as the path. Try to reduce the likelihood of false positives.

    if debug.getlocal(level, -2) then
        error('Not a module (multiple varargs): ' .. info.source)
    end

    if type(path) ~= 'string' then
        error('Not a module (argument not a string): ' .. info.source)
    end

    if not package.loaded[path] then
        error('Not a module (not in package.loaded): ' .. info.source)
    end

    -- Make sure we don't load modules as fb.util.init; use fb.util instead
    local parts, is_init = split_module_path(path)
    if is_init then
        error('Do not load modules named "init": ' .. path)
    end

    -- Indicate if the module was loaded from a file named "init.lua";
    -- if that's the case, the path is already at package level; otherwise,
    -- the last component of the path is the module within a package.
    return parts, (r == 'init')
end

-- Return the current module
local function current_module()
    local parts, package_level = current_module_parts()
    return table.concat(parts, '.'), package_level
end
M.current_module = current_module

-- Relative import
local function import(rel_path)
    -- Absolute import, same as require()
    if rel_path:sub(1, 1) ~= '.' then
        return require(rel_path)
    end

    local parts, package_level = current_module_parts()
    if not package_level then
        -- go to package level
        table.remove(parts)
    end

    local rel_parts = split_module_path(rel_path:sub(2))
    for i = 1, #rel_parts do
        local p = rel_parts[i]
        if p == '' then
            if not table.remove(parts) then
                error('Trying to go above root level')
            end
        else
            table.insert(parts, p)
        end
    end
    return require(table.concat(parts, '.'))
end
M.import = import

ffi.cdef([=[
void* getOnce(const char*);
bool lockOnce(void*);
void unlockOnce(void*, bool);
]=])

local function get_once(key)
    return util_lib.getOnce(key)
end
M.get_once = get_once

local ALREADY_CALLED = {}
M.ALREADY_CALLED = ALREADY_CALLED

-- Return ALREADY_CALLED if this once record object has already been used.
-- Otherwise, behave like pcall_locked, below.
local function pcall_once(once, func, ...)
    if not util_lib.lockOnce(once) then
        return ALREADY_CALLED
    end
    local ok, r = pcall(func, ...)
    util_lib.unlockOnce(once, ok)
    return ok, r
end
M.pcall_once = pcall_once

ffi.cdef([=[
void* getMutex(const char*);
void lockMutex(void*);
void unlockMutex(void*);
]=])

local function get_mutex(key)
    local mutex = util_lib.getMutex(key)
    if mutex == nullptr then
        error('Cannot allocate memory')
    end
    return mutex
end
M.get_mutex = get_mutex

-- Similar to pcall, but ensures that only one call to pcall_locked with the
-- same mutex may execute concurrently in this process.
local function pcall_locked(mutex, func, ...)
    util_lib.lockMutex(mutex)
    local ok, result = pcall(func, ...)
    util_lib.unlockMutex(mutex)
    return ok, result
end
M.pcall_locked = pcall_locked

local function call_locked(mutex, func, ...)
    local ok, r = pcall_locked(mutex, func, ...)
    if not ok then error(r) end
    return r
end
M.call_locked = call_locked

ffi.cdef([=[
int setenv(const char*, const char*, int);
int unsetenv(const char*);
]=])

local function setenv(name, value, overwrite)
    local overwrite_flag = overwrite and 1 or 0
    if ffi.C.setenv(name, value, overwrite_flag) == -1 then
        error('setenv failed: ' .. tostring(ffi.errno()))
    end
end
M.setenv = setenv

local function unsetenv(name)
    if ffi.C.unsetenv(name) == -1 then
        error('unsetenv failed: ' .. tostring(ffi.errno()))
    end
end
M.unsetenv = unsetenv

-- Return a UTF-8 character at index 'index' in string 's'.
--
-- Returns 2 values: code_point, length
-- - code_point is the Unicode code point of the character
-- - length is the length (in bytes) of the character; the next valid
--   Unicode character is at position 'index + length' in the string.
--
-- On error, the behavior behaves according to the skip_errors argument:
-- - if skip_errors is true, the function returns 3 values:
--   false, length, error_message
--   - length is the minimal length of the invalid sequence (so 'index + length'
--     is the earlier position where a valid sequence could begin)
--   - error_message is a human-readable error message
-- - if skip_errors is false, an error is thrown
--
local function utf8_get(s, index, skip_errors)
    local function fail(n, msg)
        msg = 'Invalid UTF-8: ' .. msg
        if not skip_errors then
            error(msg)
        end
        return false, n, msg
    end

    local function get_byte(i)
        local bi = string.byte(s, index + i)
        if not bi then
            return fail(i, 'string ends mid-sequence')
        end
        if bit.band(bi, 0xc0) ~= 0x80 then
            return fail(i, 'non-continuation byte inside sequence')
        end
        return bi
    end

    local v = string.byte(s, index)
    if not v then
        return nil, 0 -- past the end
    end

    local n
    if v >= 0xf8 then
        n = v >= 0xfc and 5 or 4

        -- Even though the sequence is invalid, make sure we fail as early as
        -- possible, so fail if there's an invalid byte in this sequence.
        for i = 1, n do
            local bi, ii, msg = get_byte(i)
            if bi == false then
                return bi, ii, msg
            end
        end

        return fail(n + 1, 'sequence too long (' .. n + 1 .. ' bytes)')
    elseif v >= 0xf0 then
        n = 3
    elseif v >= 0xe0 then
        n = 2
    elseif v >= 0xc0 then
        n = 1
    elseif v >= 0x80 then
        return fail(1, 'continuation byte at beginning of sequence')
    else
        return v, 1
    end

    v = bit.band(v, bit.lshift(1, 6-n) - 1)
    for i = 1, n do
        local bi, ii, msg = get_byte(i)
        if bi == false then
            return bi, ii, msg
        end
        v = bit.lshift(v, 6) + bit.band(bi, 0x3f)
    end

    if (v < 0x80 or
        (v < 0x800 and n >= 2) or
        (v < 0x10000 and n >= 3)) then
        return fail(n + 1, 'overlong representation')
    end

    if v >= 0xd800 and v < 0xe000 then
        return fail(n + 1, 'surrogate')
    end

    if v >= 0x110000 then
        return fail(n + 1, 'past end of Unicode range')
    end

    return v, n + 1
end
M.utf8_get = utf8_get

-- Iterate through a string as a sequence of Unicode characters.
--
-- Usage: for ch in utf8_iter(string) do ... end
--
-- - n is the first position in the string to start iterating (default 1)
-- - skip_errors controls how invalid UTF-8 is handled:
--   - if true, the iterator returns false for invalid characters (and
--     you may continue with the next character); the iterator returns a
--     second argument with the error message in this case
--   - if false, iteration stops and an error is thrown
--
local function utf8_iter(s, n, skip_errors)
    n = n or 1

    return function()
        local v, len, msg = utf8_get(s, n, skip_errors)
        n = n + len
        return v, msg
    end
end
M.utf8_iter = utf8_iter

return M
