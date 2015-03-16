--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

-- Thrift serialization / deserialization for Lua objects.
--
-- Supports all Lua types except coroutines (including functions, which are
-- serialized as bytecode, together with their upvalues), as well as torch
-- Tensor and Storage objects.
--
-- The core functions are to_file / to_string for serialization,
-- from_file / from_string for deserialization.
--
-- local thrift = require('fb.thrift')
--
-- thrift.to_file(obj, file, [codec, [envs]])
--   Serialize obj to an open Lua io file (opened with io.open, etc)
--   - codec, if specified, indicates the compression method to use; the valid
--     values are thrift.codec.NONE (no compression, default), LZ4, SNAPPY,
--     ZLIB, LZMA2.
--   - envs, if specified, is a table of environments -- that is, a table
--     of tables. Values found in these tables are not serialized -- a name
--     is serialized instead. The same envs must be given at deserialization
--     time.
--
-- thrift.to_string(obj, [codec, [envs]])
--   Return a Lua string with the serialized version of obj.
--
-- thrift.from_file(file, [envs])
--   Deserialize an object from the file and return it, advancing the
--   file pointer past the object.
--
-- thrift.from_string(str, [envs])
--   Deserialize an object from the string and return it.
--
-- Torch and Penlight classes are handled specially (see below):
-- - Torch classes only serialize data members, not methods. They serialize
--   the (globally unique, as Torch requires) type name instead of the
--   metatable.
-- - Similarly, Penlight classes only serialize data members, not methods,
--   if explicitly registered with add_penlight_class / add_penlight_classes.
--   As Penlight class names are not globally unique, you must register
--   the classes explicitly in order to ensure a unique name -> type mapping.
--
-- The "envs" feature deserves additional explanation. It is used to prevent
-- certain objects from being serialized and deserialized, as long as you
-- can guarantee that they will be available at deserialization time.
--
-- It may be used, for example, to avoid serializing modules (that are
-- upvalues to serialized functions) or objects of types that can't be
-- serialized (ffi cdata).
--
-- Example:
-- thrift.to_file(obj, file, thrift.codec.NONE,
--                {package.loaded, {pl, foo}})
-- will not attempt to serialize loaded packages, or references to "pl" or
-- "foo".
--
-- Note that envs is a table of tables to make it easy to concatenate
-- existing tables (such as package.loaded, _G, etc).
--
-- The exact same "envs" must be given at deserialization time.

local ffi = require('ffi')
local lib = require('fb.thrift.lib')
local torch = require('torch')
local pl = require('pl.import_into')()

local M = {}

local FilePtr = ffi.typeof('struct { void* p; }')

-- FFI converts from Lua file objects to FILE*, but that's not directly
-- accessible from the standard Lua/C API. We'll encode the FILE* as a Lua
-- string and decode it in libthrift.
local function encode_file(f)
    assert(io.type(f) == 'file')
    local fp = FilePtr(f)
    return ffi.string(fp, ffi.sizeof(fp))
end
M.encode_file = encode_file

local function is_primitive(v)
    local t = type(v)
    return t == 'number' or t == 'string' or t == 'boolean'
end

-- Invert a table of tables; return a map from each value in an inner table
-- to a one {outer_table_key, inner_table_key} pair that maps to that value.
-- That is, for every v such that envs[k1][k2] exists and k1 and k2 are
-- both primitive, set result[v] = {k1, k2} in the returned table. If there
-- are more than one such {k1, k2} for the same value, only one is returned.
local function invert_envs(envs)
    if envs == nil then
        return nil
    end
    local inverted = {}
    for env, values in pairs(envs) do
        if is_primitive(env) then
            for k, v in pairs(values) do
                if is_primitive(k) then
                    if not inverted[v] then
                        inverted[v] = {env, k}
                    end
                end
            end
        end
    end
    return inverted
end
M.invert_envs = invert_envs

-- Similar to to_string (below), but you are responsible for calling
-- invert_envs directly; this is useful if you want to cache the same
-- envs across calls.
local function to_string_inv(obj, codec, inverted_envs, chunk_size)
    return lib._to_string(obj, codec, inverted_envs, chunk_size)
end
M.to_string_inv = to_string_inv

-- Serialize to a Lua string
-- str = to_string(obj)
local function to_string(obj, codec, envs, chunk_size)
    return to_string_inv(obj, codec, invert_envs(envs), chunk_size)
end
M.to_string = to_string

-- Similar to to_file (below), but you are responsible for calling
-- invert_envs directly; this is useful if you want to cache the same
-- envs across calls.
local function to_file_inv(obj, f, codec, inverted_envs, chunk_size)
    return lib._to_file(obj, encode_file(f), codec, inverted_envs, chunk_size)
end
M.to_file_inv = to_file_inv

-- Serialize to a Lua open file
local function to_file(obj, f, codec, envs, chunk_size)
    return to_file_inv(obj, f, codec, invert_envs(envs), chunk_size)
end
M.to_file = to_file

-- Deserialize from a Lua string
local function from_string(s, envs)
    return lib._from_string(s, envs)
end
M.from_string = from_string

-- Deserialize from a Lua open file; the file pointer is moved past the data.
local function from_file(f, envs)
    return lib._from_file(encode_file(f), envs)
end
M.from_file = from_file

M.codec = lib.codec

local special_callbacks = {}

-- Add special callbacks for serializing / deserializing custom table types.
--
-- This is useful for OOP implementations: we'd like to serialize an identifier
-- (class name) plus the useful parts of the instance definition, rather than
-- bytecode for methods.
--
-- The 'key' must be a unique string that is used to find the proper
-- deserializer to call. The only requirement is that it is unique, and that
-- you call the add_special_callbacks() with the same arguments at
-- deserialization time.
--
-- You then specify 3 callbacks:
--
-- check(obj): return true if your serializer can serialize this type.
--
-- serialize(obj): returns a 3-element tuple:
--     id, table, metatable
--     - 'id' is a Lua object that allows you to create an object of the
--       proper type at deserialization time; usually, it's a string describing
--       the type.
--     - 'table' is the Lua object to serialize instead of obj. If you
--       return nil, we'll serialize obj.
--     - 'metatable' is the Lua object to serialize instead of obj's metatable.
--       If you return nil, we'll serialize obj's metatable; if you return
--       false, we don't serialize a metatable.
--
-- deserialize(id, obj): mutates obj in place to become the object you desire.
--     obj is a table (the same table that was serialized at serialize() time);
--     obj's metatable is set to the metatable that was serialized at
--     serialize() time.
local function add_special_callbacks(key, check, serialize, deserialize)
    if special_callbacks[key] then
        error('Duplicate key: ' .. key)
    end
    special_callbacks[key] = {check, serialize, deserialize}
end
M.add_special_callbacks = special_callbacks

-- Simple special_callback to serialize objects with a given metatable.
-- All objects with the given metatable are serialized under the given key;
-- at deserialization time, we set the metatable back. The metatable itself
-- is not serialized.
-- This is sufficient for most OOP mechanisms, where the "class" of an object
-- is its metatable.
local function add_metatable(key, mt, serializer, deserializer)
    add_special_callbacks(
        'thrift.metatable.' .. key,
        function(obj) return getmetatable(obj) == mt end,
        function(obj)
            local sobj
            if serializer then
                sobj = serializer(obj)
                assert(sobj == nil or type(sobj) == 'table')
            end
            return '', sobj or obj, false
        end,
        function(name, obj)
            if deserializer then
                local r = deserializer(obj)
                assert(r == nil)  -- No, don't return a replacement object.
            end
            setmetatable(obj, mt)
        end)
end
M.add_metatable = add_metatable

-- Check if this is a torch object
local function torch_check(obj)
    local tn = torch.typename(obj)
    return tn and torch.getmetatable(tn)
end

-- Serialize torch object; use the type as the id. Do not serialize the
-- metatable.
--
-- If the class has the specially-named methods _thrift_serialize and
-- _thrift_deserialize, they will be called as follows:
--
-- At serialization time, before an object of that class is serialized,
-- we call obj:_thrift_serialize(). This method must either return a table
-- (that will be serialized *instead of* obj) or nil (which means we'll
-- serialize obj; this gives you an opportunity to blow away temporary
-- fields that shouldn't be serialized.)
--
-- At deserialization time, we call _thrift_deserialize(obj) which must
-- mutate obj *in place*. obj is a table (not yet blessed as a Torch
-- class), and _thrift_deserialize must set fields appropriately so that
-- it can be blessed and produce a valid object.
--
-- If the class doesn't have these methods, all fields will be serialized.
local function torch_serialize(obj)
    local sobj
    if obj._thrift_serialize then
        sobj = obj:_thrift_serialize()
        assert(sobj == nil or type(sobj) == 'table')
    end
    return torch.typename(obj), sobj or obj, false
end

-- Deserialize torch object.
local function torch_deserialize(typename, obj)
    local metatable = torch.getmetatable(typename)
    if not metatable then
        error('Invalid torch typename ' .. typename)
    end
    if metatable._thrift_deserialize then
        metatable._thrift_deserialize(obj)
    end
    setmetatable(obj, metatable)
end

-- Set special serialization / deserialization callbacks for torch objects
add_special_callbacks('thrift.torch',
                      torch_check, torch_serialize, torch_deserialize)

-- Serialization callback from the C library; try all of our callbacks,
-- in order.
local function serialize_callback(obj)
    for k, v in pairs(special_callbacks) do
        local check, serialize, _ = unpack(v)
        if check(obj) then
            return k, serialize(obj)
        end
    end
end

-- Deserialization callback from the C library; dispatch by key.
local function deserialize_callback(key, ...)
    local v = special_callbacks[key]
    if not v then
        error('Invalid key ' .. key)
    end
    local deserialize = v[3]
    return deserialize(...)
end

-- Register our callbacks with the C library.
lib._set_callbacks(serialize_callback, deserialize_callback)

-- Is this a Penlight class?  this is the best we can do...
local function is_penlight_class(v)
    return type(v) == 'table' and getmetatable(v) and v._class == v
end

-- Add a Penlight class to be serialized under the given name (must
-- be globally unique).
--
-- If the class has the specially-named methods _thrift_serialize and
-- _thrift_deserialize, they will be called as follows:
--
-- At serialization time, before an object of that class is serialized,
-- we call obj:_thrift_serialize(). This method must either return a table
-- (that will be serialized *instead of* obj) or nil (which means we'll
-- serialize obj; this gives you an opportunity to blow away temporary
-- fields that shouldn't be serialized.)
--
-- At deserialization time, we call _thrift_deserialize(obj) which must
-- mutate obj *in place*. obj is a table (not yet blessed as a Penlight
-- class), and _thrift_deserialize must set fields appropriately so that
-- it can be blessed and produce a valid object.
--
-- If the class doesn't have these methods, all fields will be serialized.
local function add_penlight_class(cls, name)
    assert(name)
    assert(is_penlight_class(cls))
    add_metatable(name, cls, cls._thrift_serialize, cls._thrift_deserialize)
end
M.add_penlight_class = add_penlight_class

-- Register all Penlight classes in a table for serialization / deserialization.
-- The name is obtained by concatenating prefix to the table key.
local function add_penlight_classes(tab, prefix)
    if prefix then
        prefix = prefix .. '.'
    else
        prefix = ''
    end
    for k, v in pairs(tab) do
        if is_penlight_class(v) then
            add_penlight_class(v, prefix .. k)
        end
    end
end
M.add_penlight_classes = add_penlight_classes

add_penlight_class(pl.List, 'pl.List')
add_penlight_class(pl.Set, 'pl.Set')

return M
