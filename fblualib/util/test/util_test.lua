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
    assertEquals(pl.List({'a'}), pl.List(deque))
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

function testFilterKeys()
    local a = {foo = 42, bar = 50, baz = 100}
    local b = util.filter_keys(a, {'foo', 'baz'})
    assertEquals({foo = 42, baz = 100}, b)
end

function testDefaultDictBehavesAsExpected()
    local d = util.defaultdict(function() return 50 end)
    assertEquals(d[1], 50)
    assertEquals(d.cat, 50)
    d[1] = 10
    assertEquals(d[1], 10)
    d.dog = 20
    assertEquals(d.dog, 20)
end

function testDefaultDictGivesSeparateInstances()
    local d = util.defaultdict(function() return {} end)
    d[1][1] = 2
    d[2][1] = 3
    assertEquals(d[1][1], 2)
    assertEquals(d[2][1], 3)
end

function testIsList()
    assertTrue(util.is_list({}))
    assertTrue(util.is_list({'a'}))
    assertTrue(util.is_list({'a', 'b', 42}))
    assertFalse(util.is_list({a = 10}))
    assertFalse(util.is_list({[1] = 'a', [3] = 'b'}))
    assertFalse(util.is_list({[0] = 'a', [1] = 'b', [2] = 'c'}))
    local t = {'a', 'b', 'c', 'd'}
    assertTrue(util.is_list(t))
    t[3] = nil
    assertFalse(util.is_list(t))
end

function testImport()
    assertEquals(1111, util.import('.imports'))
end

function testPcallOnce()
    local key = 'fb.util.test.once1'
    local once = util.get_once(key)

    local ok, result = util.pcall_once(once, error, 'hello')
    assertFalse(ok)
    assertTrue(string.match(result, 'hello'))

    ok, result = util.pcall_once(once, function() return 'foo' end)
    assertTrue(ok)
    assertEquals('foo', result)

    ok, result = util.pcall_once(once, function() return 'foo' end)
    assertTrue(ok == util.ALREADY_CALLED)
    assertEquals(nil, result)
end

-- Not actually testing mutual exclusion; testing single-threaded behavior
-- only (and that the lock gets released on error)
function testPcallLocked()
    local key = 'fb.util.test.pcall_locked1'
    local mutex = util.get_mutex(key)

    local ok, result = util.pcall_locked(
        mutex,
        function(x) return x + 10 end,
        42)
    assertTrue(ok)
    assertEquals(52, result)

    ok, result = util.pcall_locked(mutex, error, 'hello')
    assertFalse(ok)
    assertTrue(string.match(result, 'hello'))

    ok, result = util.pcall_locked(
        mutex,
        function(x) return x + 10 end,
        42)
    assertTrue(ok)
    assertEquals(52, result)
end

function testSplitLines()
    assertEquals({}, util.splitlines(''))
    assertEquals({}, util.splitlines('', true))
    assertEquals({''}, util.splitlines('\n'))
    assertEquals({'\n'}, util.splitlines('\n', true))
    assertEquals({'foo'}, util.splitlines('foo'))
    assertEquals({'foo'}, util.splitlines('foo', true))
    assertEquals({'foo'}, util.splitlines('foo\n'))
    assertEquals({'foo\n'}, util.splitlines('foo\n', true))
    assertEquals({'foo', 'bar'}, util.splitlines('foo\nbar'))
    assertEquals({'foo\n', 'bar'}, util.splitlines('foo\nbar', true))
    assertEquals({'foo', 'bar'}, util.splitlines('foo\nbar\n'))
    assertEquals({'foo\n', 'bar\n'}, util.splitlines('foo\nbar\n', true))
    assertEquals({'foo', '', 'bar'}, util.splitlines('foo\n\nbar'))
    assertEquals({'foo\n', '\n', 'bar'}, util.splitlines('foo\n\nbar', true))
    assertEquals({'foo', '', 'bar'}, util.splitlines('foo\n\nbar\n'))
    assertEquals({'foo\n', '\n', 'bar\n'},
                 util.splitlines('foo\n\nbar\n', true))
    assertEquals({'foo', ''}, util.splitlines('foo\n\n'))
    assertEquals({'foo\n', '\n'}, util.splitlines('foo\n\n', true))
end

function testIndentLines()
    assertEquals('', util.indent_lines('', 'xx'))
    assertEquals('xx\n', util.indent_lines('\n', 'xx'))
    assertEquals('xxabc', util.indent_lines('abc', 'xx'))
    assertEquals('xxabc\n', util.indent_lines('abc\n', 'xx'))
    assertEquals('xxabc\nxxdef', util.indent_lines('abc\ndef', 'xx'))
    assertEquals('xxabc\nxxdef\n', util.indent_lines('abc\ndef\n', 'xx'))
    assertEquals('xxabc\nxx\nxxdef', util.indent_lines('abc\n\ndef', 'xx'))
    assertEquals('xxabc\nxx\nxxdef\n', util.indent_lines('abc\n\ndef\n', 'xx'))
end

function testSetenv()
    local var = 'FBLUALIB_UTIL_TEST_VAR'
    util.unsetenv(var)
    assertEquals(nil, os.getenv(var))
    util.setenv(var, 'hello')
    assertEquals('hello', os.getenv(var))
    util.setenv(var, 'world')  -- no overwrite
    assertEquals('hello', os.getenv(var))
    util.setenv(var, 'world', true)  -- overwrite
    assertEquals('world', os.getenv(var))
    util.unsetenv(var)
    assertEquals(nil, os.getenv(var))
end

local function test_utf8(s, ...)
    local i = 1
    for c, err in util.utf8_iter(s, 1, true) do
        local expected = select(i, ...)
        if c == false then
            assertEquals('Invalid UTF-8: ' .. expected, err)
        else
            assertEquals(expected, c)
        end
        i = i + 1
    end
    assertEquals(nil, select(i, ...))
end

function testUTF8()
    test_utf8(
        'f\xc3\xa7 \xe4\xb8\x82!\xf0\x9f\x92\xa9',
        string.byte('f'),
        0xe7,
        string.byte(' '),
        0x4e02,
        string.byte('!'),
        0x1f4a9)

    test_utf8('')

    -- Stress test examples from
    -- https://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-test.txt

    test_utf8(
        '\x00\xc2\x80\xe0\xa0\x80' ..
        '\xf0\x90\x80\x80' ..
        '\xf4\x90\x80\x80' ..
        '\xf8\x88\x80\x80\x80' ..
        '\xfc\x84\x80\x80\x80\x80q',
        0,
        0x80,
        0x800,
        0x10000,
        'past end of Unicode range',
        'sequence too long (5 bytes)',
        'sequence too long (6 bytes)',
        string.byte('q'))

    test_utf8(
        '\x7f\xdf\xbf\xef\xbf\xbf' ..
        '\xf4\x8f\xbf\xbf' ..
        '\xf7\xbf\xbf\xbf' ..
        '\xfb\xbf\xbf\xbf\xbf' ..
        '\xfd\xbf\xbf\xbf\xbf\xbfq',
        0x7f,
        0x7ff,
        0xffff,
        0x10ffff,
        'past end of Unicode range',
        'sequence too long (5 bytes)',
        'sequence too long (6 bytes)',
        string.byte('q'))

    test_utf8(
        '\x80\xbfq\xbf\x80q',
        'continuation byte at beginning of sequence',
        'continuation byte at beginning of sequence',
        string.byte('q'),
        'continuation byte at beginning of sequence',
        'continuation byte at beginning of sequence',
        string.byte('q'))

    test_utf8(
        '\xc0a\xdfb\xe0c\xefd\xf0e\xf7f\xf8g\xfbh\xfci\xfdj',
        'non-continuation byte inside sequence',
        string.byte('a'),
        'non-continuation byte inside sequence',
        string.byte('b'),
        'non-continuation byte inside sequence',
        string.byte('c'),
        'non-continuation byte inside sequence',
        string.byte('d'),
        'non-continuation byte inside sequence',
        string.byte('e'),
        'non-continuation byte inside sequence',
        string.byte('f'),
        'non-continuation byte inside sequence',
        string.byte('g'),
        'non-continuation byte inside sequence',
        string.byte('h'),
        'non-continuation byte inside sequence',
        string.byte('i'),
        'non-continuation byte inside sequence',
        string.byte('j'))

    test_utf8(
        '\xe0\x80a\xf0\x80b\xf0\x80\x80c',
        'non-continuation byte inside sequence',
        string.byte('a'),
        'non-continuation byte inside sequence',
        string.byte('b'),
        'non-continuation byte inside sequence',
        string.byte('c'))

    test_utf8(
        '\xc0',
        'string ends mid-sequence')

    test_utf8(
        '\xe0\x80',
        'string ends mid-sequence')

    test_utf8(
        '\xf0\x80\x80',
        'string ends mid-sequence')

    test_utf8(
        '\xc0\xaf\xe0\x80\xaf\xf0\x80\x80\xaf',
        'overlong representation',
        'overlong representation',
        'overlong representation')

    test_utf8(
        '\xed\xa0\x80\xed\xbf\xbf',
        'surrogate',
        'surrogate')

end

LuaUnit:main()
