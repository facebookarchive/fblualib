--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

require('fb.luaunit')

local pl = require('pl.import_into')()
local torch = require('torch')
local thrift = require('fb.thrift')
local util = require('fb.util')

local codec = thrift.codec.LZMA2
local function check(obj)
    assertEquals(obj, thrift.from_string(thrift.to_string(obj, codec)))
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

function testThriftSerialization()
    check(nil)
    check(10)
    check(0.5)
    check('hello')
    check(true)
    check(false)
    check({})
    check({a = 10, b = 20})
    check({a = 'hello', b = 'world'})
    check({a = 'hello', [10] = 'world'})
    check({'hello', 'world', 'goodbye'})
    check({[10] = 'a', [20] = true})
    check({a = 10, b = {a = 'hello', b = 'world'}})

    local a = {'hello', 'world'}
    local b = {a, a}
    check(b)
    b[1] = b

    -- assertEquals will overflow the stack here...
    local r = thrift.from_string(thrift.to_string(b))
    assertTrue(r[1] == r)
    assertEquals(a, r[2])

    local t = torch.randn(10, 10)
    assertTensorEquals(t, thrift.from_string(thrift.to_string(t)))
end

function testThriftSerializationBigSparseTables()
    local function size(t)
        local c = 0
        for k, v in pairs(t) do
            c = c + 1
        end
        return c
    end

    local function gen_huge_sparse_table()
        local t = { }
        -- Fill it with sparse entries until #t returns something bogus.
        while #t == 0 do
            t[math.random(3000)] = 4
        end
        assert(size(t) ~= #t)
        return t
    end

    local function tables_equal(t1, t2)
        for k,v in pairs(t1) do
            if not t2[k] then
                error(("missing key %s"):format(k))
            end
            if v ~= t2[k] then
                error(("wrong value for key %s: is %s, should be %s"):format(
                       k, t2[k], t1[k]))
            end
        end
        return true
    end

    local t = gen_huge_sparse_table()
    local tt = thrift.from_string(thrift.to_string(t))
    -- NB: you are not guaranteed that #t == #tt!
    assert(tables_equal(t, tt))
end

function testThriftSerializationToFile()
    local file = io.tmpfile()

    thrift.to_file('hello', file, thrift.codec.LZMA2)
    thrift.to_file({'hello', 'world'}, file, thrift.codec.ZLIB)
    thrift.to_file(42, file)
    file:seek('set', 0)
    assertEquals('hello', thrift.from_file(file))
    assertEquals({'hello', 'world'}, thrift.from_file(file))
    assertEquals(42, thrift.from_file(file))
end

function testThriftSerializationFunction()
    local u1 = 10
    local f1 = function(x) return u1 + x end
    local f2 = thrift.from_string(thrift.to_string(f1))
    assertEquals(42, f2(32))
end

-- Generate a randomized object
local function generate()
    -- One of the things we can generate is a reference to a previously-created
    -- object; save those here.
    local existing_refs = {}
    local depth = 0
    local max_depth = 10

    local function ref_name(i)
        return string.format('ref%d', i)
    end

    -- Record a references in existing_refs
    local function record(lua_obj)
        table.insert(existing_refs, lua_obj)
        return lua_obj
    end

    local function gen_int()
        return math.random(100)
    end

    local function gen_float()
        return math.random(100) + 0.5
    end

    local function gen_str()
        local len = math.random(20)
        local s = ''
        for i = 1, len do
            s = s ..
                string.char(math.random(string.byte('a'), string.byte('z')))
        end
        return record(s)
    end

    local gen_key
    local gen_value

    local function gen_list()
        local lua_obj = {}
        while math.random() <= 0.8 do
            local lua_item = gen_value()
            if lua_item then
                table.insert(lua_obj, lua_item)
            end
        end
        return record(lua_obj)
    end

    local function gen_dict()
        local lua_obj = {}
        while math.random() <= 0.8 do
            local lua_key
            repeat
                lua_key = gen_key()
            -- make sure we're not treating it as a list, and no dupes
            until lua_key and lua_key ~= 1 and not lua_obj[lua_key]

            local lua_value = gen_value()
            lua_obj[lua_key] = lua_value
        end
        return record(lua_obj)
    end

    local function gen_ref()
        if #existing_refs == 0 then
            return
        end
        local i = math.random(#existing_refs)
        return existing_refs[i]
    end

    local function gen_table()
        if depth >= max_depth then
            return
        end
        local r = math.random()
        depth = depth + 1
        local lua_obj
        if r <= 0.5 then
            lua_obj = gen_list()
        else
            lua_obj = gen_dict()
        end
        depth = depth - 1
        return lua_obj
    end

    function gen_key()
        local r = math.random()
        if r <= 0.4 then
            return gen_int()
        elseif r <= 0.8 then
            return gen_str()
        else
            return gen_float()
        end
    end

    function gen_value()
        local r = math.random()
        if r <= 0.2 then
            return gen_int()
        elseif r <= 0.3 then
            return gen_float()
        elseif r <= 0.4 then
            return gen_str()
        elseif r <= 0.5 then
            return gen_ref()
        else
            return gen_table()
        end
    end

    local lua_obj
    repeat
        -- gen_ref may return nil
        lua_obj = gen_value()
    until lua_obj

    return lua_obj
end

function testRandomized()
    local seed = math.floor(util.time() * 1000)
    print(string.format('Random seed is %d', seed))
    math.randomseed(seed)
    for i = 1, 10 do
        local lua_obj = generate()
        local converted = thrift.from_string(thrift.to_string(lua_obj))
        assertEquals(lua_obj, converted)
    end
end

function testRandomizedChunked()
    local seed = math.floor(util.time() * 1000)
    print(string.format('Random seed is %d', seed))
    math.randomseed(seed)
    for i = 1, 10 do
        local lua_obj = generate()
        local converted = thrift.from_string(thrift.to_string(
            lua_obj, thrift.codec.NONE, 10))
        assertEquals(lua_obj, converted)
    end
end

function testMetatable()
    local obj = {foo = 23, bar = 42}
    setmetatable(obj, {__index = function(k) return 100 end})
    local converted = thrift.from_string(thrift.to_string(obj))
    assertEquals(23, converted.foo)
    assertEquals(42, converted.bar)
    assertEquals(100, converted.no_such_thing)
end

function testBasicOOP()
    local Foo = {}
    Foo.__index = Foo
    Foo._foo = 42

    function Foo:new()
        local obj = {}
        setmetatable(obj, self)
        return obj
    end

    function Foo:foo()
        return self._foo + 10
    end

    obj = Foo:new()

    local converted = thrift.from_string(thrift.to_string(obj))
    assertEquals(42, converted._foo)
    assertEquals(52, converted:foo())

    obj._foo = 100
    converted = thrift.from_string(thrift.to_string(obj))
    assertEquals(100, converted._foo)
    assertEquals(110, converted:foo())
end

-- global
thrift_test = {}

local Base = torch.class('thrift_test.Base')

function Base:__init(a, b)
    self.a = a
    self.b = b
    self.c = torch.randn(a, b)
    self.s = torch.Storage(a * b):fill(42)
end

local Derived, parent = torch.class('thrift_test.Derived', 'thrift_test.Base')
assert(parent == Base)

function Derived:__init(a, b)
    parent.__init(self, a, b)
    self.d = torch.randn(a * 2, b * 2)
end

function testTorchOOP()
    local obj = thrift_test.Derived(5, 6)
    assertEquals('thrift_test.Derived', torch.typename(obj))
    local converted = thrift.from_string(thrift.to_string(obj))
    assertEquals('thrift_test.Derived', torch.typename(converted))
    assertEquals(obj.a, converted.a)
    assertEquals(obj.b, converted.b)
    assertTensorEquals(obj.c, converted.c)
    assertTensorEquals(obj.d, converted.d)
    assertEquals(obj.s:totable(), converted.s:totable())
end

local ExplicitSerDe = torch.class('thrift_test.ExplicitSerDe')

function ExplicitSerDe:__init(a, b, serialize_in_place)
    self.a = a
    self.b = b
    self.serialize_in_place = serialize_in_place
    self.state = 'initialized'
end

function ExplicitSerDe:_thrift_serialize()
    if self.serialize_in_place then
        self.state = nil
        return nil
    end
    return {a=self.a, b=self.b}
end

function ExplicitSerDe:_thrift_deserialize()
    assert(self.a and self.b)
    assert(not self.state)
    self.state = 'deserialized'
end

local function testSerDe(in_place)
    local obj = thrift_test.ExplicitSerDe(5, 6, in_place)
    assert(obj.state == 'initialized')
    local converted = thrift.from_string(thrift.to_string(obj))
    assert(converted.a == 5)
    assert(converted.b == 6)
    assert(converted.state == 'deserialized')
end

function testTorchOOPExplicitSerDeInPlace()
    testSerDe(true)
end

function testTorchOOPExplicitSerDeOutOfPlace()
    testSerDe(false)
end

function testBM()
    local t1 = {}
    for i = 1, 10000 do
        table.insert(t1, 'hello' .. i)
    end

    local t2 = {}
    for i = 1, 10000 do
        t2['hello' .. i] = i
    end

    local s = os.clock()
    for i = 1, 10 do
        thrift.to_string(t1)
        thrift.to_string(t2)
    end
    local e = os.clock()
    print('Serialize Thrift: tables', e - s)

    s = os.clock()
    for i = 1, 10 do
        torch.serialize(t1)
        torch.serialize(t2)
    end
    e = os.clock()
    print('Serialize torch : tables', e - s)

    local s1 = thrift.to_string(t1)
    local s2 = thrift.to_string(t2)

    s = os.clock()
    for i = 1, 10 do
        thrift.from_string(s1)
        thrift.from_string(s2)
    end
    e = os.clock()
    print('Deserialize Thrift: tables', e - s)

    s1 = torch.serialize(t1)
    s2 = torch.serialize(t2)

    s = os.clock()
    for i = 1, 10 do
        torch.deserialize(s1)
        torch.deserialize(s2)
    end
    e = os.clock()
    print('Deserialize torch : tables', e - s)

    local tensor1 = torch.randn(100, 100)

    s = os.clock()
    for i = 1, 10000 do
        thrift.to_string(tensor1)
    end
    e = os.clock()
    print('Serialize Thrift: tensor', e - s)

    s = os.clock()
    for i = 1, 10000 do
        torch.serialize(tensor1)
    end
    e = os.clock()
    print('Serialize torch : tensor', e - s)

    local st1 = thrift.to_string(tensor1)

    s = os.clock()
    for i = 1, 10000 do
        thrift.from_string(st1)
    end
    e = os.clock()
    print('Deserialize Thrift: tensor', e - s)

    st1 = torch.serialize(tensor1)
    s = os.clock()
    for i = 1, 10000 do
        torch.deserialize(st1)
    end
    e = os.clock()
    print('Deserialize torch : tensor', e - s)
end

function testPenlightClasses()
    local A = pl.class()
    function A:_init()
        self.a = 42
    end

    local B = pl.class(A)
    function B:_init()
        self:super()
        self.b = 100
    end

    thrift.add_penlight_class(A, 'A')
    thrift.add_penlight_class(B, 'B')

    local b = B()
    local b1 = thrift.from_string(thrift.to_string(b))

    assertTrue(B:class_of(b1))
    assertEquals(42, b.a)
    assertEquals(100, b.b)
end

function testPenlightClassesCustomSerialization()
    local A = pl.class()
    function A:_init()
        self.a = 42
        self.b = 100
    end

    function A:_thrift_serialize()
        return {a = self.a}  -- not b
    end

    function A:_thrift_deserialize()
        self.deserialized = true
    end

    thrift.add_penlight_class(A, 'A1')

    local a = thrift.from_string(thrift.to_string(A()))

    assertTrue(A:class_of(a))
    assertEquals(42, a.a)
    assertEquals(nil, a.b)
    assertTrue(a.deserialized)
end

LuaUnit:main()
