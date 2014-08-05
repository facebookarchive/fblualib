--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

require('fb.luaunit')

local ffivector = require('fb.ffivector')
local gettorch = require('torch')

function testIntFFIVector()
    local v = ffivector.new_int()
    v:resize(42)
    assertEquals(42, #v)
    assertTrue(v:capacity() >= 42)

    for i = 1, 42 do
        assertEquals(0, v[i])
    end

    assertError(function() return v[0] end)
    assertError(function() return v[43] end)

    for i = 1, 42 do
        v[i] = i * 10
    end

    for i = 1, 42 do
        assertEquals(i * 10, v[i])
    end
end

function testStructFFIVector()
    local v = ffivector.new('struct { int a; int64_t b; }')

    v:resize(42)
    for i = 1, 42 do
        p = v[i]  -- it's a reference!
        p.a = i * 10
        p.b = i * 100
    end

    for i = 1, 42 do
        assertEquals(i * 10, v[i].a)
        assertEquals(i * 100, tonumber(v[i].b))
    end
end

function testStringFFIVector()
    local v = ffivector.new_string()

    v:resize(42)
    for i = 1, 42 do
        v[i] = 'hello ' .. tostring(i)
    end
    collectgarbage()  -- temp strings should no longer be allocated

    for i = 1, 42 do
        assertEquals('hello ' .. tostring(i), v[i])
    end
end

function testStringFFIVectorDestruction()
    do
        local v = ffivector.new_string()
    end
    collectgarbage()
end

function testFFIVectorDestruction()
    local destructor_count = 0

    do
        local v = ffivector.new(
            'int', 0, tonumber, nil,
            function(x) destructor_count = destructor_count + 1 end)
        v:resize(42)
        assertEquals(0, destructor_count)

        v[10] = 10
        assertEquals(1, destructor_count)

        v[42] = 10
        assertEquals(2, destructor_count)

        v[43] = 10
        assertEquals(2, destructor_count)
        assertEquals(43, #v)

        v:resize(50)
        assertEquals(2, destructor_count)
        assertEquals(50, #v)

        v:resize(40)
        assertEquals(12, destructor_count)
        assertEquals(40, #v)
    end
    collectgarbage()

    assertEquals(52, destructor_count)
end

LuaUnit:main()
