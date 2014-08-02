--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

local pl = require('pl.import_into')()
local util = require('fb.util')

require('fb.luaunit')

function testLongestCommonPrefix()
    local lcp = util.longest_common_prefix
    assertEquals('', lcp({}))
    assertEquals('', lcp({''}))
    assertEquals('', lcp({'', ''}))
    assertEquals('foo', lcp({'foo'}))
    assertEquals('', lcp({'foo', ''}))
    assertEquals('', lcp({'', 'foo'}))
    assertEquals('foo', lcp({'foo', 'foobar'}))
    assertEquals('foo', lcp({'foobar', 'foo'}))
    assertEquals('fo', lcp({'foo', 'foobar', 'fox'}))
    assertEquals('', lcp({'foo', '', 'fox'}))
end

function testDequeSimple()
    local deque = util.Deque()
    assertEquals(0, #deque)

    deque:push_back('a')
    assertEquals(1, #deque)
    assertEquals(1, deque.first)
    assertEquals(1, deque.last)

    assertEquals(pl.List({'a'}), pl.List(deque))

    deque:push_back('b')
    assertEquals(2, #deque)
    assertEquals(1, deque.first)
    assertEquals(2, deque.last)

    assertEquals(pl.List({'a', 'b'}), pl.List(deque))

    deque:push_front('c')
    assertEquals(3, #deque)
    assertEquals(0, deque.first)
    assertEquals(2, deque.last)

    assertEquals(pl.List({'c', 'a', 'b'}), pl.List(deque))

    assertEquals('c', deque[1])
    assertEquals('a', deque[2])
    assertEquals('b', deque[3])

    deque[3] = 'd'

    assertEquals('c', deque:get_stable(0))
    assertEquals('a', deque:get_stable(1))
    assertEquals('d', deque:get_stable(2))

    assertEquals('d', deque:pop_back())
    assertEquals(2, #deque)
    assertEquals(0, deque.first)
    assertEquals(1, deque.last)

    assertEquals(pl.List({'c', 'a'}), pl.List(deque))
    deque:set_stable(0, 'e')

    assertEquals('e', deque[1])
    assertEquals('a', deque[2])

    assertEquals('e', deque:get_stable(0))
    assertEquals('a', deque:get_stable(1))

    assertEquals('e', deque:pop_front())
    assertEquals(1, #deque)
    assertEquals(1, deque.first)
    assertEquals(1, deque.last)

    assertEquals('a', deque[1])
    assertEquals('a', deque:get_stable(1))
end

function testSetDefaultNoStore()
    local table = {a = {'x'}, b = {'y'}}
    util.set_default(table, function() return {'z'} end)
    assertEquals({'x'}, table.a)
    assertEquals({'y'}, table.b)
    assertEquals({'z'}, table.c)
    assertEquals(nil, rawget(table, 'c'))

    -- not updated, as the returned value is a temporary
    table.c[2] = 'u'
    assertEquals({'z'}, table.c)
    assertEquals(nil, rawget(table, 'c'))

    table.c = {'v'}
    assertEquals({'v'}, table.c)
    assertEquals({'v'}, rawget(table, 'c'))
end

function testSetDefaultStore()
    local table = {a = {'x'}, b = {'y'}}
    util.set_default(table, function() return {'z'} end, true)
    assertEquals({'x'}, table.a)
    assertEquals({'y'}, table.b)
    assertEquals({'z'}, table.c)
    assertEquals({'z'}, rawget(table, 'c'))
    assertEquals({'z'}, table.d)

    -- updated, as the returned value is now in the table
    table.c[2] = 'u'
    assertEquals({'z', 'u'}, table.c)
    assertEquals({'z'}, table.d)  -- different objects
end

function testCreateTempDir()
    local dir = util.create_temp_dir('hello')
    assertTrue(dir:match('/hello%.[^/]*$'))
    assertTrue(pl.path.isdir(dir))
    pl.dir.rmtree(dir)
end

function testRandomSeed()
    -- Not much to test; let's just test that consecutive calls return
    -- different values.
    local a = util.random_seed()
    local b = util.random_seed()
    assertFalse(a == b)
end

function testClocks()
    local start_realtime = util.time()
    local start_monotonic = util.monotonic_clock()
    util.sleep(100e-3)  -- 100ms
    local end_realtime = util.time()
    local end_monotonic = util.monotonic_clock()
    -- assert that at least 80 ms have passed
    assertTrue(end_realtime - start_realtime >= 80e-3)
    assertTrue(end_monotonic - start_monotonic >= 80e-3)
end

function testStdString()
    local s = util.std_string('foo')
    assertEquals('foo', tostring(s))
    assertEquals(3, #s)

    local s1 = util.std_string(s)
    assertEquals('foo', tostring(s1))
    assertEquals(3, #s1)

    s:append('bar')
    assertEquals('foobar', tostring(s))
    assertEquals(6, #s)
    assertEquals('foo', tostring(s1))
    assertEquals(3, #s1)

    local s2 = s .. 'baz'
    assertEquals('foobarbaz', tostring(s2))
    assertEquals(9, #s2)
    assertEquals('foobar', tostring(s))
    assertEquals(6, #s)
    assertEquals('foo', tostring(s1))
    assertEquals(3, #s1)

    s2:append(s1)
    assertEquals('foobarbazfoo', tostring(s2))
    assertEquals(12, #s2)
    assertEquals('foobar', tostring(s))
    assertEquals(6, #s)
    assertEquals('foo', tostring(s1))
    assertEquals(3, #s1)

    local s3 = s2 .. s1
    assertEquals('foobarbazfoofoo', tostring(s3))
    assertEquals(15, #s3)
    assertEquals('foobarbazfoo', tostring(s2))
    assertEquals(12, #s2)
    assertEquals('foobar', tostring(s))
    assertEquals(6, #s)
    assertEquals('foo', tostring(s1))
    assertEquals(3, #s1)

    assertEquals('f', s3:sub(1, 1))
    assertEquals('f', s3:sub(0, 1))
    assertEquals('', s3:sub(0, 0))
    assertEquals('oob', s3 :sub(2, 4))
    assertEquals('', s3:sub(2, 1))
    assertEquals('oobarbazfoofoo', s3:sub(2, 200))
    assertEquals('oobarbazfoof', s3:sub(2, -3))
    assertEquals('', s3:sub(2, -200))
    assertEquals('oofo', s3:sub(-5, -2))

    s3:erase(13)
    assertEquals('foobarbazfoo', tostring(s3))
    s3:erase(9, 10)
    assertEquals('foobarbaoo', tostring(s3))
    s3:insert(7, 'xyz')
    assertEquals('foobarxyzbaoo', tostring(s3))
    s3:replace(2, 4, 'meow')
    assertEquals('fmeowarxyzbaoo', tostring(s3))
end

function testCEscape()
    assertEquals('fo\\"o\\nbar', util.c_escape('fo"o\nbar'))
end

function testCUnescape()
    assertEquals('fo"o\nbar', util.c_unescape('fo\\"o\\nbar'))
    assertErrorMessage('invalid escape sequence', util.c_unescape, '\\q')
end

LuaUnit:main()
