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

-- Serialize to a Lua string
-- str = to_string(obj)
M.to_string = lib.to_string

-- Serialize to a Lua open file
local function to_file(obj, f, codec)
    return lib._to_file(obj, encode_file(f), codec)
end
M.to_file = to_file

-- Deserialize from a Lua string
-- obj = from_string(str)
M.from_string = lib.from_string

-- Deserialize from a Lua open file; the file pointer is moved past the data.
local function from_file(f, codec)
    return lib._from_file(encode_file(f), codec)
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
local function torch_serialize(obj)
    return torch.typename(obj), obj, false
end

-- Deserialize torch object.
local function torch_deserialize(typename, obj)
    local metatable = torch.getmetatable(typename)
    if not metatable then
        error('Invalid torch typename ' .. typename)
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

-- Register all Penlight classes in a table for serialization / deserialization
local function add_penlight_classes(tab, prefix)
    for k, v in pairs(tab) do
        if is_penlight_class(v) then
            add_penlight_class(v, prefix .. '.' .. k)
        end
    end
end
M.add_penlight_classes = add_penlight_classes

add_penlight_class(pl.List, 'pl.List')
add_penlight_class(pl.Set, 'pl.Set')

return M
