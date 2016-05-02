--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

require('fb.luaunit')

local torch = require('torch')
local thrift = require('fb.thrift')
local lib = require('fb.thrift.test.lib')

local function check_nil()
    assertEquals(nil, thrift.from_string(lib.write_nil()))
    lib.read_nil(thrift.to_string(nil))
end

local function check_bool(val)
    assertEquals(val, thrift.from_string(lib.write_bool(val)))
    assertEquals(val, lib.read_bool(thrift.to_string(val)))
end

local function check_double(val)
    assertEquals(val, thrift.from_string(lib.write_double(val)))
    assertEquals(val, lib.read_double(thrift.to_string(val)))
end

local function check_string(val)
    assertEquals(val, thrift.from_string(lib.write_string(val)))
    assertEquals(val, lib.read_string(thrift.to_string(val)))
end

local function assertTensorEquals(t1, t2)
    assertEquals(t1:dim(), t2:dim())

    for i = 1, t1:dim() do
        assertEquals(t1:size(i), t2:size(i))
    end

    t1 = t1:contiguous()
    t2 = t2:contiguous()

    t1:resize(t1:nElement())
    t2:resize(t2:nElement())

    for i = 1, t1:nElement() do
        assertEquals(t1[i], t2[i])
    end
end

local function check_tensor(val)
    assertTensorEquals(val, thrift.from_string(lib.write_tensor(val)))
    assertTensorEquals(val, lib.read_tensor(thrift.to_string(val)))
end

function testLuaObject()
    check_nil()
    check_bool(false)
    check_bool(true)
    check_double(42)
    check_double(42.5)
    check_string('')
    check_string('hello world')
    local t = torch.DoubleTensor():rand(5, 10)
    check_tensor(t)
end

function testTableIteration()
    local t = {
        [1] = 10,
        [2] = 20,
        [3] = 30,
        [true] = 40,
        [false] = 50,
        hello = 60,
        world = 70,
        [100] = 80,
        [200] = 90,
    }
    lib.check_table_iteration(thrift.to_string(t))
end

LuaUnit:main()
