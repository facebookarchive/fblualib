--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

require('fb.luaunit')

local ffi = require('ffi')
local py = require('fb.python')
local torch = require('torch')
local util = require('fb.util')

-- Test if fundamental types work both ways. This is useful for everything
-- else.
function testFundamental()
    assertEquals(true, py.eval('True'))
    assertEquals(false, py.eval('False'))
    assertEquals(true, py.eval('a', {a=true}))
    assertEquals(false, py.eval('a', {a=false}))
    assertEquals(nil, py.eval('None'))
    -- Can't set nil value in table, use placeholder
    assertEquals(nil, py.eval('a', {a=py.None}))
    assertEquals(42, py.eval('42'))
    assertEquals(42.5, py.eval('42.5'))
    assertEquals(42, py.eval('long(42)'))
    assertEquals(42, py.eval('a', {a=42}))
    assertEquals('hello', py.eval('"hello"'))
    assertEquals('hello', py.eval('u"hello"'))  -- utf8
    assertEquals('hello', py.eval('a', {a='hello'}))
    assertEquals(true, py.eval('isinstance(a, float)', {a=42}))
    assertEquals(true, py.eval('isinstance(a, bytes)', {a='hello'}))
end

function testSequence()
    assertEquals({2,3}, py.eval('[2,3]'))
    assertEquals({2,3}, py.eval('a', {a={2,3}}))
    assertEquals(true, py.eval('isinstance(a, list)', {a={2,3}}))
    assertEquals({}, py.eval('[]'))
    assertEquals({2,3}, py.eval('(2,3)'))
    assertEquals({}, py.eval('()'))
    assertEquals({2,{3,4},5}, py.eval('[2,[3,4],5]'))
    assertEquals({2,{3,4},5}, py.eval('a', {a={2,{3,4},5}}))

    -- test identity; identical objects should convert to
    -- identical objects
    py.exec('ts_a = [2,3]; ts_b = [ts_a, ts_a]')
    local r = py.eval('ts_b')
    assertEquals({{2,3},{2,3}}, r)
    assertTrue(r[1] == r[2])

    local a = {2,3}
    local b = {a,a}
    r = py.eval('b', {b=b})
    assertEquals({{2,3},{2,3}}, r)
    assertTrue(r[1] == r[2])
    assertEquals(true, py.eval('id(b[0]) == id(b[1])', {b=b}))
end

function testMapping()
    -- Empty Lua table converts to empty Python dict. This is debatable
    -- (could be empty Python list).
    assertEquals({}, py.eval('{}'))
    assertEquals({}, py.eval('a', {a={}}))
    assertEquals(true, py.eval('isinstance(a, dict)', {a={}}))

    assertEquals({[2] = 3}, py.eval('{2:3}'))
    assertEquals({[2] = 3}, py.eval('a', {a={[2] = 3}}))
    assertEquals(true, py.eval('isinstance(a, dict)', {a={}}))

    assertEquals({foo = 3}, py.eval('{"foo":3}'))
    assertEquals({foo = 3}, py.eval('a', {a={foo = 3}}))

    -- test identity; identical objects should convert to
    -- identical objects
    py.exec('td_a = {"foo":3}; td_b = {"foo": td_a, "bar": td_a}')
    local r = py.eval('td_b')
    assertEquals({foo = {foo = 3}, bar = {foo = 3}}, r)
    assertTrue(r.foo == r.bar)

    local a = {foo = 3}
    local b = {foo = a, bar = a}
    r = py.eval('b', {b=b})
    assertEquals({foo = {foo = 3}, bar = {foo = 3}}, r)
    assertTrue(r.foo == r.bar)
    assertEquals(true, py.eval('id(b["foo"]) == id(b["bar"])', {b=b}))
end

local function assertTensorEquals(a, b)
    assertEquals(0, (a - b):sum())
end

function testTensor()
    local a = torch.Tensor(10,10)
    for i = 1, 10 do
        for j = 1, 10 do
            a[{i,j}] = i * 10 + j
        end
    end
    assertEquals(10, a:stride(1))
    assertEquals(1, a:stride(2))

    py.exec([=[
import numpy as np

tt_a = np.ndarray([10, 10])
for i in range(10):
    for j in range(10):
        tt_a[i][j] = (i + 1) * 10 + (j + 1)
]=])
    assertEquals(true, py.eval('np.array_equal(a, tt_a)', {a=a}))
    assertEquals(10 * ffi.sizeof('double'), py.eval('tt_a.strides[0]'))
    assertEquals(ffi.sizeof('double'), py.eval('tt_a.strides[1]'))
    assertEquals(10 * ffi.sizeof('double'), py.eval('a.strides[0]', {a=a}))
    assertEquals(ffi.sizeof('double'), py.eval('a.strides[1]', {a=a}))
    local r = py.eval('tt_a')
    assertTensorEquals(a, py.eval('tt_a'))
    assertEquals(10, r:stride(1))
    assertEquals(1, r:stride(2))

    a = a:t()
    assertEquals(1, a:stride(1))
    assertEquals(10, a:stride(2))

    py.exec([=[
tt_a = np.transpose(tt_a)
]=])

    assertEquals(true, py.eval('np.array_equal(a, tt_a)', {a=a}))
    assertEquals(ffi.sizeof('double'), py.eval('tt_a.strides[0]'))
    assertEquals(10 * ffi.sizeof('double'), py.eval('tt_a.strides[1]'))
    assertEquals(ffi.sizeof('double'), py.eval('a.strides[0]', {a=a}))
    assertEquals(10 * ffi.sizeof('double'), py.eval('a.strides[1]', {a=a}))
    local r = py.eval('tt_a')
    assertTensorEquals(a, py.eval('tt_a'))
    assertEquals(1, r:stride(1))
    assertEquals(10, r:stride(2))
end

function testRefs()
    py.exec([=[
import string
]=])
    local pstring = py.reval('string')
    assertEquals('0123456789', py.eval(pstring.digits))
    local s = py.bytes('hello {0} {1}')
    assertEquals('hello world 42.0', py.eval(s.format('world', 42)))
end

function testCoerce()
    assertEquals('hello', py.eval(py.bytes('hello')))
    assertEquals(true, py.eval('isinstance(a, bytes)', {a=py.bytes('hello')}))
    assertEquals('hello', py.eval(py.unicode('hello')))
    assertEquals(true, py.eval('isinstance(a, unicode)',
                               {a=py.unicode('hello')}))
    assertEquals(42, py.eval(py.int(42)))
    assertEquals(true, py.eval('isinstance(a, int)', {a=py.int(42)}))
    assertEquals(42, py.eval(py.long(42)))
    assertEquals(true, py.eval('isinstance(a, long)', {a=py.long(42)}))
    assertEquals(42, py.eval(py.float(42)))
    assertEquals(true, py.eval('isinstance(a, float)', {a=py.float(42)}))
    assertEquals({1,2}, py.eval(py.list({1,2})))
    assertEquals(true, py.eval('isinstance(a, list)', {a=py.list({1,2})}))
    assertEquals({1,2}, py.eval(py.tuple({1,2})))
    assertEquals(true, py.eval('isinstance(a, tuple)', {a=py.tuple({1,2})}))
    assertEquals({1,2}, py.eval(py.dict({1,2})))
    assertEquals(true, py.eval('isinstance(a, dict)', {a=py.dict({1,2})}))
end

function testVarargs()
    py.exec([=[
def get_args(*args, **kwargs):
    d = dict(kwargs)
    for i,a in enumerate(args):
        d[i] = a
    return d
]=])
    local get_args = py.reval('get_args')

    assertEquals({}, py.eval(get_args()))

    -- regular args only
    assertEquals({[0]=1, [1]=2}, py.eval(get_args(1, 2)))

    -- varargs only
    assertEquals({}, py.eval(get_args(py.args, {})))
    assertEquals({[0]=1, [1]=2}, py.eval(get_args(py.args, {1, 2})))

    -- kwargs only
    assertEquals({}, py.eval(get_args(py.kwargs, {})))
    assertEquals({a=foo, b=bar}, py.eval(get_args(py.kwargs, {a=foo, b=bar})))

    -- regular + varargs
    assertEquals({[0]=1, [1]=2, [2]=3, [3]=4},
                 py.eval(get_args(1, 2, py.args, {3, 4})))

    -- regular + kwargs
    assertEquals({[0]=1, [1]=2, a=foo, b=bar},
                 py.eval(get_args(1, 2, py.kwargs, {a=foo, b=bar})))

    -- regular + varargs + kwargs
    assertEquals({[0]=1, [1]=2, [2]=3, [3]=4, a=foo, b=bar},
                 py.eval(get_args(1, 2, py.args, {3, 4}, py.kwargs,
                                  {a=foo, b=bar})))
end

function testVarargsWithRefs()
    py.exec([=[
def get_args(*args, **kwargs):
    d = dict(kwargs)
    for i,a in enumerate(args):
        d[i] = a
    return d

l = [3,4]
d = {'a': 'foo', 'b': 'bar'}
]=])
    local get_args = py.reval('get_args')
    local l = py.reval('l')
    local d = py.reval('d')
    assertEquals({[0]=1, [1]=2, [2]=3, [3]=4, a=foo, b=bar},
                 py.eval(get_args(1, 2, py.args, l, py.kwargs, d)))
end

function testOperators()
    local i42 = py.int(42)
    assertEquals(-42, py.eval(-i42))

    local i2 = py.int(2)
    assertEquals(44, py.eval(i42 + i2))
    assertEquals(44, py.eval(i42 + 2))
    assertEquals(44, py.eval(42 + i2))
    assertEquals(40, py.eval(i42 - i2))
    assertEquals(40, py.eval(i42 - 2))
    assertEquals(40, py.eval(42 - i2))
    assertEquals(84, py.eval(i42 * i2))
    assertEquals(84, py.eval(i42 * 2))
    assertEquals(84, py.eval(42 * i2))
    assertEquals(21, py.eval(i42 / i2))
    assertEquals(21, py.eval(i42 / 2))
    assertEquals(21, py.eval(42 / i2))
    assertEquals(0, py.eval(i42 % i2))
    assertEquals(0, py.eval(i42 % 2))
    assertEquals(0, py.eval(42 % i2))
    assertEquals(1764, py.eval(i42 ^ i2))
    assertEquals(1764, py.eval(i42 ^ 2))
    assertEquals(1764, py.eval(42 ^ i2))

    assertTrue(i2 < i42)
    assertFalse(i42 < i2)
    assertFalse(i2 > i42)
    assertTrue(i42 > i2)
    assertTrue(i2 <= i42)
    assertFalse(i42 <= i2)
    assertFalse(i2 >= i42)
    assertTrue(i42 >= i2)
    assertFalse(i2 == i42)
    assertTrue(i2 ~= i42)

    local i2b = py.int(2)
    assertFalse(i2 < i2b)
    assertFalse(i2 > i2b)
    assertTrue(i2 <= i2b)
    assertTrue(i2 >= i2b)
    assertTrue(i2 == i2b)
    assertFalse(i2 ~= i2b)


    local ifoo = py.bytes('foo')
    assertEquals(3, #ifoo)

    local ibar = py.bytes('bar')
    assertEquals('foobar', py.eval(ifoo .. ibar))
    assertEquals('foobar', py.eval(ifoo .. 'bar'))
    assertEquals('foobar', py.eval('foo' .. ibar))
end

function testImport()
    local string_module = py.import('string')
    local digits = py.eval(string_module.digits)
    assertEquals('0123456789', digits)
end

function testNoLeaks()
    py.exec([=[
import gc
import numpy as np
]=])
    do
        local np = py.reval('np')
        local t = torch.Tensor(10, 10)
        local nt1 = py.eval('np.transpose(t)', {t=t})
        local nt2 = np.transpose(nt1)
        local nt3 = np.transpose(t)
    end
    collectgarbage()
    py.exec('gc.collect()')
    py._check_no_refs()

    do
        local t = py.eval('np.ndarray([10,10])')
        local t1 = t:t()
        local nt1 = py.eval('np.transpose(t)', {t=t})
    end
    collectgarbage()
    py.exec('gc.collect()')
    py._check_no_refs()

    do
        local a = py.int(100)
        local b = py.int(200)
        local c = py.eval('a + 10', {a=a})
        local d = a + 10
        local e = a + b
    end
    collectgarbage()
    py.exec('gc.collect()')
    py._check_no_refs()
end

-- Generate a randomized object in three forms: Lua object, Python object
-- (using opaque references), and Python string (which must evaluate to the
-- same as the Python object)
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
    local function record(lua_obj, py_obj, py_str)
        table.insert(existing_refs, {lua_obj, py_str})
        -- References are variables on the Python side
        py.exec(string.format('%s = %s', ref_name(#existing_refs), py_str))
        return lua_obj, py_obj, py_str
    end

    -- Generate an int; randomly choose Python int or long
    local function gen_int()
        local n = math.random(100)
        local py_obj
        if math.random() < 0.5 then
            py_obj = py.int(n)
        else
            py_obj = py.long(n)
        end
        return n, py_obj, tostring(n)
    end

    -- Generate a float. We only use values that we know can be converted back
    -- to the same value after reading from a string.
    local function gen_float()
        local n = math.random(100) + 0.5
        return n, py.float(n), tostring(n)
    end

    -- Generate a string. We only use lower-case letters to avoid having to
    -- deal with escaping.
    local function gen_str()
        local len = math.random(20)
        local s = ''
        for i = 1, len do
            s = s ..
                string.char(math.random(string.byte('a'), string.byte('z')))
        end
        local py_obj
        if math.random() < 0.5 then
            py_obj = py.str(s)
        else
            py_obj = py.unicode(s)
        end
        return record(s, py_obj, "'" .. s .. "'")
    end

    local gen_value

    -- Generate a list, which may become a Python list or a tuple
    local function gen_list()
        local lua_obj = {}
        local py_obj = py.list({})
        local is_tuple = math.random() < 0.5
        local py_str
        if is_tuple then
            py_str = '('
        else
            py_str = '['
        end
        local first = true
        while math.random() <= 0.8 do
            local lua_item, py_item_obj, py_item_str = gen_value()
            if lua_item then
                table.insert(lua_obj, lua_item)
                if not first then
                    py_str = py_str .. ', '
                end
                first = false
                py_obj.append(py_item_obj)
                py_str = py_str .. py_item_str
            end
        end
        if is_tuple then
            -- (foo) is an expression, not a tuple, so (foo,)
            if #lua_obj == 1 then
                py_str = py_str .. ','
            end
            py_str = py_str .. ')'
            py_obj = py.tuple(py_obj)
        else
            py_str = py_str .. ']'
        end
        return record(lua_obj, py_obj, py_str)
    end

    -- Generate the key for a dictionary
    local function gen_key()
        if math.random() < 0.5 then
            return gen_int()
        else
            return gen_str()
        end
    end

    -- Generate a dictionary
    local function gen_dict()
        local lua_obj = {}
        local py_obj = py.dict({})
        local py_str = '{'
        local first = true
        while math.random() <= 0.8 do
            local lua_key, py_key_obj, py_key_str
            repeat
                lua_key, py_key_obj, py_key_str = gen_key()
            -- make sure we're not treating it as a list, and no dupes
            until lua_key ~= 1 and not lua_obj[lua_key]

            local lua_value, py_value_obj, py_value_str = gen_value()
            if lua_value then
                lua_obj[lua_key] = lua_value
                if not first then
                    py_str = py_str .. ', '
                end
                first = false
                py_obj[py_key_obj] = py_value_obj
                py_str = py_str .. py_key_str .. ': ' .. py_value_str
            end
        end
        py_str = py_str .. '}'
        return record(lua_obj, py_obj, py_str)
    end

    -- Generate a reference to some existing object
    function gen_ref()
        if #existing_refs == 0 then
            return
        end
        local i = math.random(#existing_refs)
        return existing_refs[i][1], py.reval(ref_name(i)), ref_name(i)
    end

    -- Return a value
    function gen_value()
        local r = math.random()
        local lua_obj, py_str
        if r <= 0.2 then
            return gen_int()
        elseif r <= 0.3 then
            return gen_float()
        elseif r <= 0.4 then
            return gen_str()
        elseif r <= 0.5 then
            return gen_ref()
        elseif depth >= max_depth then
            return
        else
            local lua_obj, py_obj, py_str
            depth = depth + 1
            if r <= 0.7 then
                lua_obj, py_obj, py_str = gen_list()
            else
                lua_obj, py_obj, py_str = gen_dict()
            end
            depth = depth - 1
            return lua_obj, py_obj, py_str
        end
    end

    local lua_obj, py_obj, py_str
    repeat
        -- gen_ref may return nil
        lua_obj, py_obj, py_str = gen_value()
    until lua_obj

    return lua_obj, py_obj, py_str
end

function testRandomized()
    do
        local seed = math.floor(util.time() * 10000)
        print(string.format('Random seed is %d', seed))
        math.randomseed(seed)
        local lua_obj, py_obj, py_str = generate()
        local converted_from_str = py.eval(py_str)
        local converted_from_obj = py.eval(py_obj)
        assertEquals(lua_obj, converted_from_str)
        assertEquals(lua_obj, converted_from_obj)
    end
    py.exec('import gc')
    collectgarbage()
    py.exec('gc.collect()')
    py._check_no_refs()
end


LuaUnit:main()
