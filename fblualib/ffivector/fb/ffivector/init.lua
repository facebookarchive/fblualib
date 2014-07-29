--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

-- This class implements a vector of LuaJIT cdata objects. Importantly, the
-- objects are stored outside of the Lua heap, and will not count toward the
-- 1GiB Lua heap limit.
--
-- Any cdata object is allowed, as long as it can be moved safely in memory by
-- calling memmove() (or equivalent); objects that store pointers to (parts of)
-- themselves cannot be used. Here's a contrived example of an invalid object:
--
-- struct Foo {
--   int elements[100];
--   int* currentElement;
-- };
--
-- Foo foo; foo.currentElement = &(foo.elements[20]);
--
-- foo.currentElement points within foo, and therefore the pointer would become
-- invalid if foo were relocated to another address.
--
-- Usage: there is one main function:
--
-- ffivector.new(ctype, initial_capacity, index, newindex, destructor)
--
-- ctype is the type of the elements, either a ffi ctype (ffi.typeof('double'))
-- or a string containing a C declaration ('double' or
-- 'struct { double x; int y; }')
--
-- initial_capacity is the initial capacity of the vector, default 0
--
-- index is an optional function that will be applied to elements as they
-- are retrieved from the vector. For example, a vector of int could use
-- 'tonumber' here, and then v[42] would return a Lua number rather than a
-- FFI int. The vector of string implementation uses this to convert from
-- the internal representation to a Lua string (created on the fly).
--
-- newindex is the opposite; it's an optional function that will be applied to
-- new values as they are added to the vector. The vector of string
-- implementation uses this to convert from Lua strings to the internal
-- representation. Vectors of FFI numeric types don't need this, as LuaJIT
-- performs the conversion automatically.
--
-- destructor is an optional function that is called on each element as it
-- is destroyed (when the vector itself is GCed, or when an element is
-- overwritten). The vector of string implementation uses this to free memory.
--
-- For convenience, we also provide functions to create vectors of standard
-- C numeric types: new_char, new_unsigned_long, new_int, new_uint64_t,
-- new_float, etc. The vectors such created have "tonumber" as the index
-- function, and so precision is limited by Lua numbers.
--
-- We also provide new_string, which creates a vector of strings. The strings
-- are stored outside the Lua heap, and a new Lua string is created on demand
-- whenever you access an element.
--
-- local ffivector = require('fb.ffivector')
--
-- local vec = ffivector.new_string()
-- vec:resize(42)
-- -- use vec[1] through vec[42]
-- -- assigning to one past the end is supported as a special case and will
-- -- grow the vector, as this is a common pattern for Lua tables
-- -- (vec[43] = 'foo')

local ffi = require('ffi')
local module_config = require('fb.ffivector._config')
local lib = ffi.load(module_config.clib)

local M = {}

-- NOTE: this must match ffivector.h
ffi.cdef([[
typedef struct {
  size_t elementSize;
  size_t size;
  size_t capacity;
  void* data;
} FFIVector;

int ffivector_create(FFIVector* v, size_t elementSize, size_t initialCapacity);
void ffivector_destroy(FFIVector* v);
int ffivector_reserve(FFIVector* v, size_t n);
int ffivector_resize(FFIVector* v, size_t n);

void* malloc(size_t size);
void free(void* ptr);
]])

-- iterator returned by pairs() and ipairs()
local function iter(vec, idx)
    if idx and idx < #vec then
        idx = idx + 1
        return idx, vec[idx]
    end
end

-- helper function to generate the tuple for ipairs()
local function ipairs_fn(t)
    return iter, t, 0
end

local cachedTypes = {}
setmetatable(cachedTypes, {__mode = 'v'})  -- weak values

local function new(ctype, initial_capacity, index, newindex, destructor)
    ctype = ffi.typeof(ctype)  -- support both ctype and C declaration
    local factory = cachedTypes[ctype]

    if not factory then
        local pointerType = ffi.typeof('$*', ctype)

        local methods = {}
        function methods:resize(n)
            local size = tonumber(self._v.size)
            if destructor and n < size then
                local ptr = ffi.cast(pointerType, self._v.data)
                for i = n + 1, size do
                    destructor(ptr[i - 1])
                end
            end

            if lib.ffivector_resize(self._v, n) < 0 then
                error('Out of memory')
            end
        end

        function methods:reserve(n)
            if lib.ffivector_resserve(self._v, n) < 0 then
                error('Out of memory')
            end
        end

        function methods:capacity()
            return self._v.capacity
        end

        -- a ffi metatype is faster than a table here...
        factory = ffi.metatype('struct { FFIVector _v; }', {
            __new = function(ct, initial_capacity)
                initial_capacity = initial_capacity or 0
                local self = ffi.new(ct)
                if lib.ffivector_create(self._v, ffi.sizeof(ctype),
                                        initial_capacity) ~= 0 then
                    error('Failed to create vector')
                end
                return self
            end,

            __gc = function(self)
                if destructor then
                    local ptr = ffi.cast(pointerType, self._v.data)
                    local size = tonumber(self._v.size)
                    for i = 1, size do
                        destructor(ptr[i - 1])
                    end
                end
                lib.ffivector_destroy(self._v)
            end,

            __len = function(self)
                return tonumber(self._v.size)
            end,

            __index = function(self, k)
                local kn = tonumber(k)
                if kn then
                    if kn >= 1 and kn <= self._v.size then
                        local v = ffi.cast(pointerType, self._v.data)[kn - 1]
                        if index then
                            return index(v)
                        else
                            return v
                        end
                    else
                        error('Index out of range')
                    end
                else
                    return methods[k]
                end
            end,

            __newindex = function(self, k, v)
                local kn = tonumber(k)
                if kn then
                    local ptr
                    if kn >= 1 and kn <= self._v.size then
                        ptr = ffi.cast(pointerType, self._v.data)
                        if destructor then
                            destructor(ptr[kn - 1])
                        end
                    elseif kn == self._v.size + 1 then
                        self:resize(kn)
                        ptr = ffi.cast(pointerType, self._v.data)
                    else
                        error('Index out of range')
                    end
                    if newindex then
                        v = newindex(v)
                    end
                    ptr[kn - 1] = v
                else
                    error('Invalid index')
                end
            end,

            __ipairs = ipairs_fn,
            __pairs = ipairs_fn,
        })

        cachedTypes[ctype] = factory
    end

    return factory(initial_capacity)
end
M.new = new

local string_type = ffi.typeof('struct { char* data; size_t size; }')
local voidptr_type = ffi.typeof('void*')
local null = voidptr_type()

-- Create new_X functions for all numeric types

-- These get a 'u' prefix as well (so int8_t, uint8_t, etc)
local c99_int_types = { 'int8_t', 'int16_t', 'int32_t', 'int64_t' }

-- These get a 'unsigned ' prefix as well (so int, unsigned int, etc)
local int_types = { 'char', 'short', 'int', 'long' }

-- These are extra. Yes, signed char, as 'char', 'signed char', and
-- 'unsigned char' are actually three different types
local types = {'float', 'double', 'signed char'}

for _, t in ipairs(int_types) do
    table.insert(types, t)
    table.insert(types, 'unsigned ' .. t)
end
for _, t in ipairs(c99_int_types) do
    table.insert(types, t)
    table.insert(types, 'u' .. t)
end

for _, t in ipairs(types) do
    local ctype = ffi.typeof(t)
    t = t:gsub(' ', '_')
    M[t] = ctype

    local fn = 'new_' .. t
    M[fn] = function(initial_capacity)
        return new(ctype, initial_capacity, tonumber)
    end
end

local function create_ffi_string(src)
    local ptr = ffi.C.malloc(#src)
    if ptr == null then
        error('Failed to allocate memory')
    end
    ffi.copy(ptr, src, #src)
    return string_type(ptr, #src)
end

local function create_lua_string(src)
    return ffi.string(src.data, src.size)
end

local function destroy_ffi_string(src)
    ffi.C.free(src.data)
end

local function new_string(initial_capacity)
    return new(string_type, initial_capacity,
               create_lua_string, create_ffi_string, destroy_ffi_string)
end
M.new_string = new_string

return M
