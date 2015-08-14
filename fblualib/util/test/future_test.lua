--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

require('fb.luaunit')
local future = require('fb.util.future')

local function tsmatch(str, pattern)
    return string.match(tostring(str), pattern)
end

function testCallbacksAlreadyCompletedSuccessfully()
    local succ = 0
    local fail = 0
    local comp = 0

    local f = future.make(10, nil, 20):on_success(
      function(v) succ = succ + v end):on_error(
      function(v) fail = fail + v end):on_completion(
      function(v) comp = comp + 1 end)

    assertTrue(f:has_value())
    assertEquals(10, f:value())
    assertEquals(nil, select(2, f:value()))
    assertEquals(20, select(3, f:value()))

    assertEquals(10, succ)
    assertEquals(0, fail)
    assertEquals(1, comp)
end

function testCallbacksAlreadyCompletedWithError()
    local succ = 0
    local fail = 0
    local comp = 0

    local f = future.make_error(10):on_success(
      function(v) succ = succ + v end):on_error(
      function(v) fail = fail + v end):on_completion(
      function(v) comp = comp + 1 end)

    assertTrue(f:has_error())
    assertEquals(10, f:error())

    local ok, result = pcall(function() return f:value() end)
    assertFalse(ok)
    assertTrue(tsmatch(result, '10$'))

    assertEquals(0, succ)
    assertEquals(10, fail)
    assertEquals(1, comp)
end

function testCallbacksSuccessfulCompletion()
    local succ = 0
    local fail = 0
    local comp = 0

    local p = future.Promise()
    local f = p:future():on_success(
      function(v) succ = succ + v end):on_error(
      function(v) fail = fail + v end):on_completion(
      function(v) comp = comp + 1 end)

    assertEquals(0, succ)
    assertEquals(0, fail)
    assertEquals(0, comp)

    p:set_value(10, nil, 20)

    assertTrue(f:has_value())
    assertEquals(10, f:value())
    assertEquals(nil, select(2, f:value()))
    assertEquals(20, select(3, f:value()))

    assertEquals(10, succ)
    assertEquals(0, fail)
    assertEquals(1, comp)
end

function testCallbacksCompletionWithError()
    local succ = 0
    local fail = 0
    local comp = 0

    local p = future.Promise()
    local f = p:future():on_success(
      function(v) succ = succ + v end):on_error(
      function(v) fail = fail + v end):on_completion(
      function(v) comp = comp + 1 end)

    assertEquals(0, succ)
    assertEquals(0, fail)
    assertEquals(0, comp)

    p:set_error(10)

    assertTrue(f:has_error())
    assertTrue(tsmatch(f:error(), '10$'))

    assertEquals(0, succ)
    assertEquals(10, fail)
    assertEquals(1, comp)
end

function testMapSuccessful()
    local p = future.Promise()

    local f = p:future():map(function(a, b, c) return a + c, b, a - c end)

    p:set_value(10, nil, 20)
    assertEquals(30, f:value())
    assertEquals(nil, select(2, f:value()))
    assertEquals(-10, select(3, f:value()))
end

function testMapError()
    local p = future.Promise()

    local f = p:future():map(function(v) return v + 1 end)

    p:set_error(10)
    assertTrue(tsmatch(f:error(), '10$'))
end

function testMapCallbackError()
    local p = future.Promise()

    local f = p:future():map(function(v) error('hello') end)

    p:set_value(10)
    assertTrue(tsmatch(f:error(), 'hello$'))
end

function testFlatMapSuccessful()
    local p = future.Promise()
    local x
    local p2 = future.Promise()

    local f = p:future():flat_map(
        function(a, b, c) x = a + c; return p2:future() end)

    p:set_value(10, nil, 20)
    assertTrue(f:is_pending())

    p2:set_value(x + 20, nil, 100)
    assertEquals(50, f:value())
    assertEquals(nil, select(2, f:value()))
    assertEquals(100, select(3, f:value()))
end

function testFlatMapError()
    local p = future.Promise()
    local p2 = future.Promise()

    local f = p:future():flat_map(function(v) return p2:future() end)

    p:set_error('hello')
    assertTrue(tsmatch(f:error(), 'hello$'))
end

function testFlatMapSecondError()
    local p = future.Promise()
    local p2 = future.Promise()

    local f = p:future():flat_map(function(v) return p2:future() end)

    p:set_value(10)
    assertTrue(f:is_pending())

    p2:set_error('hello')
    assertTrue(tsmatch(f:error(), 'hello$'))
end

function testFlatMapCallbackError()
    local p = future.Promise()

    local f = p:future():flat_map(function() error('hello') end)

    p:set_value(10)
    assertTrue(tsmatch(f:error(), 'hello$'))
end

function testEnforceSuccessful()
    local p = future.Promise()

    local f = p:future():enforce(function(a, b, c) return a + c == 30 end)

    p:set_value(10, nil, 20)
    assertEquals(10, f:value())
    assertEquals(nil, select(2, f:value()))
    assertEquals(20, select(3, f:value()))
end

function testEnforcePredicateFailed()
    local p = future.Promise()

    local f = p:future():enforce(function(v) return v == 10 end)

    p:set_value(20)
    assertTrue(tsmatch(f:error(), 'predicate failed$'))
end

function testEnforceError()
    local p = future.Promise()

    local f = p:future():enforce(function(v) return v == 10 end)

    p:set_error('hello')
    assertTrue(tsmatch(f:error(), 'hello$'))
end

function testEnforceCallbackError()
    local p = future.Promise()

    local f = p:future():enforce(function() error('hello') end)

    p:set_value(10)
    assertTrue(tsmatch(f:error(), 'hello$'))
end

function testAndThenSuccessful()
    local called
    local p = future.Promise()

    local f = p:future():and_then(function(a, b, c) called = a + c end)

    p:set_value(10, nil, 20)
    assertEquals(10, f:value())
    assertEquals(nil, select(2, f:value()))
    assertEquals(20, select(3, f:value()))
    assertEquals(30, called)
end

function testAndThenError()
    local called
    local p = future.Promise()

    local f = p:future():and_then(function(v) called = v + 1 end)

    p:set_error(10)
    assertTrue(tsmatch(f:error(), '10$'))
    assertEquals(nil, called)
end

function testAndThenCallbackError()
    local p = future.Promise()

    local f = p:future():and_then(function(v) error('hello') end)

    p:set_value(10)
    assertTrue(tsmatch(f:error(), 'hello$'))
end

function testAllSuccessful()
    local p1 = future.Promise()
    local p2 = future.Promise()
    local p3 = future.Promise()

    local f = future.all(p1:future(), p2:future(), p3:future())

    p1:set_value(100, nil, 200)
    p2:set_value(10, nil, 20)
    p3:set_value(1, nil, 2)

    local v1, v2, v3 = f:value()
    assertEquals({100, 10, 1}, {v1, v2, v3})
end

function testAllError()
    local p1 = future.Promise()
    local p2 = future.Promise()
    local p3 = future.Promise()

    local f = future.all(p1:future(), p2:future(), p3:future())

    p1:set_value(100)
    p2:set_error('hello')
    p3:set_value(1)

    assertEquals('hello', f:error())
end

function testAllMultiSuccessful()
    local p1 = future.Promise()
    local p2 = future.Promise()
    local p3 = future.Promise()

    local f = future.all_multi(p1:future(), p2:future(), p3:future())

    p1:set_value(100, nil, 200)
    p2:set_value(10, nil, 20)
    p3:set_value(1, nil, 2)

    local v1, v2, v3 = f:value()

    assertEquals(
        {{100, nil, 200, n=3},
         {10, nil, 20, n=3},
         {1, nil, 2, n=3}}, {v1, v2, v3})
end

function testReduceSuccessful()
    local p1 = future.Promise()
    local p2 = future.Promise()
    local p3 = future.Promise()

    local function add(a, b, c, d) return a + b + d end

    local f = future.reduce(add, 0, p1:future(), p2:future(), p3:future())

    p1:set_value(100, nil, 200)
    p2:set_value(10, nil, 20)
    p3:set_value(1, nil, 2)

    assertEquals(333, f:value())
end

-- all() is implemented in terms of reduce(), so we won't test the error
-- case for reduce() separately

function testAnyIndexSuccessful()
    local p1 = future.Promise()
    local p2 = future.Promise()
    local p3 = future.Promise()

    local f = future.any_index(p1:future(), p2:future(), p3:future())

    p2:set_value(10, nil, 20)
    p1:set_value(100, nil, 200)
    p3:set_error('hello')

    assertEquals(2, f:value())
    assertEquals(10, select(2, f:value()))
    assertEquals(nil, select(3, f:value()))
    assertEquals(20, select(4, f:value()))
end

function testAnyIndexError()
    local p1 = future.Promise()
    local p2 = future.Promise()
    local p3 = future.Promise()

    local f = future.any_index(p1:future(), p2:future(), p3:future())

    p2:set_error('hello')
    p1:set_value(100)
    p3:set_error(1)

    assertEquals('hello', f:error())
end

function testAnySuccessful()
    local p1 = future.Promise()
    local p2 = future.Promise()
    local p3 = future.Promise()

    local f = future.any(p1:future(), p2:future(), p3:future())

    p2:set_value(10, nil, 20)
    p1:set_value(100, nil, 200)
    p3:set_error('hello')

    assertEquals(10, f:value())
    assertEquals(nil, select(2, f:value()))
    assertEquals(20, select(3, f:value()))
end

function testAnyError()
    local p1 = future.Promise()
    local p2 = future.Promise()
    local p3 = future.Promise()

    local f = future.any(p1:future(), p2:future(), p3:future())

    p2:set_error('hello')
    p1:set_value(100)
    p3:set_error(1)

    assertEquals('hello', f:error())
end

LuaUnit:main()
