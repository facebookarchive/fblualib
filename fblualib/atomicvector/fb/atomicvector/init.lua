--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

local clib = require('fb.atomicvector.clib')
local torch = require('torch')
local thrift = require('fb.thrift')

local M = {
    create_float = clib.create_float,
    create_double = clib.create_double,
    get = clib.get,
    destroy = clib.destroy,
    append = clib.append
}

local SerializationFormatVersionMajor = 1
local SerializationFormatVersionMinor = 0
local SerializationFormatMagic = "antigone thistle"

-- save() takes an atomic vector and a Lua file as inputs
function M.save(atom_vec, f)
    local hdr = {
        magic = SerializationFormatMagic,
        major = SerializationFormatVersionMajor,
        minor = SerializationFormatVersionMinor,
        size = #atom_vec
    }
    thrift.to_file(hdr, f)
    for i = 1,#atom_vec do
        thrift.to_file(atom_vec[i], f)
    end
end

-- load() takes an atomic vector and a Lua file as inputs
function M.load(atom_vec, f)
    local hdr = thrift.from_file(f)
    local function enforce(b, msg)
        if not b then
            error("atomicvector deserialization error: " .. msg)
        end
    end
    local function warnUnless(b, msg)
        if not b then
            print( "atomicvector deserializatio warningn: " .. msg)
        end
    end
    enforce(type(hdr) == 'table', "bad header type: " .. type(hdr))
    enforce(hdr.magic == SerializationFormatMagic,
            "wrong magic field")
    enforce(hdr.major <= SerializationFormatVersionMajor,
            "too new version " .. hdr.major)
    enforce(type(hdr.size) == "number",
            "invalid size in header " .. hdr.size)
    warnUnless(hdr.major < SerializationFormatVersionMajor or
               hdr.minor <= SerializationFormatVersionMinor,
               ("restoring from new minor version %d.%d"):
                 format(hdr.major, hdr.minor))

    for i = 1, hdr.size do
        clib.append(atom_vec, thrift.from_file(f))
    end
end

return M
