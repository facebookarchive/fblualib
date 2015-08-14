--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

require('fb.luaunit')

local eh = require('fb.util.error')

local function assertMatches(tab, patterns)
    assertEquals(#patterns, #tab)
    for i = 1, #tab do
        assertTrue(string.match(tab[i], patterns[i]))
    end
end

function testSimple()
    local msgs = {}
    local function foo(x)
        table.insert(msgs, 'begin foo ' .. tostring(x))
        error('error from foo')
        table.insert(msgs, 'end foo')
    end

    local function bar(x)
        table.insert(msgs, 'begin bar ' .. tostring(x))
        foo(x)
        table.insert(msgs, 'end bar')
    end

    local ok = pcall(
        eh.on_error,
        bar,
        function(err) table.insert(msgs, err) end,
        42)
    assertFalse(ok)

    assertMatches(msgs, {
        'begin bar 42',
        'begin foo 42',
        'error from foo'
    })

    msgs = {}

    local ok = pcall(
        eh.finally,
        bar,
        function(err) table.insert(msgs, err) end,
        42)
    assertFalse(ok)

    assertMatches(msgs, {
        'begin bar 42',
        'begin foo 42',
        'error from foo'
    })
end

function testNoError()
    local msgs = {}
    local function foo(x)
        table.insert(msgs, 'begin foo ' .. tostring(x))
    end

    eh.on_error(
        foo,
        function(err) table.insert(msgs, 'done!') end,
        42)

    assertEquals({
        'begin foo 42',
    }, msgs)

    msgs = {}

    eh.finally(
        foo,
        function(err) table.insert(msgs, 'done!') end,
        42)

    assertEquals({
        'begin foo 42',
        'done!',
    }, msgs)
end

function testNested()
    local msgs = {}

    pcall(
        function()
            eh.on_error(
                function()
                    pcall(
                        function()
                            eh.on_error(
                                function()
                                    table.insert(msgs, '1')
                                    error('foo')
                                end,
                                function()
                                    table.insert(msgs, 'inner handler')
                                end)
                        end)
                    table.insert(msgs, '2')
                    error('bar')
                end,
                function()
                    table.insert(msgs, 'outer handler')
                end)
            end)

    assertEquals({
        '1',
        'inner handler',
        '2',
        'outer handler',
    }, msgs)
end

function testSafeToString()
    assertEquals('42', eh.safe_tostring(42))

    local foo = {}
    setmetatable(foo, {__tostring = function() return 'hello' end})
    assertEquals('hello', eh.safe_tostring(foo))

    setmetatable(foo, {__tostring = function() error('hello', 0) end})
    assertEquals('(cannot convert to string: hello)',
                 eh.safe_tostring(foo))

    setmetatable(foo, {__tostring = function() error(foo) end})
    assertEquals('(cannot convert to string: failed recursively)',
                 eh.safe_tostring(foo))
end

function testStructuredErrorHandling()
    -- string
    local ok1, result1 = pcall(function() error('foo', 0) end)
    assertFalse(ok1)
    assertEquals('foo', result1)

    -- wrapped string
    local ok2, result2 = xpcall(function() error('foo', 0) end, eh.wrap)
    assertFalse(ok2)
    assertEquals('foo', tostring(result2))
    assertTrue(result2.traceback)

    -- already wrapped when thrown
    local ok3, result3 = pcall(function() eh.throw('foo', nil, 0) end)
    assertFalse(ok3)
    assertEquals('foo', tostring(result3))
    assertTrue(result3.traceback)

    -- double-wrapping does nothing
    local ok4, result4 = xpcall(function() eh.throw('foo', nil, 0) end, eh.wrap)
    assertFalse(ok4)
    assertEquals('foo', tostring(result4))
    assertTrue(result4.traceback)

    -- similar to finally() but reimplemented here to show nested errors
    local function runner(fn, finally, ...)
        local ok, result = xpcall(fn, eh.wrap, ...)
        if finally then
            local okf, resultf = xpcall(finally, eh.wrap)
            if not okf then
                eh.throw('finally block threw an error', resultf, 0)
            end
        end
        if not ok then
            eh.throw('function threw an error', result, 0)
        end
        return result
    end

    local ok5, result5 = xpcall(runner, eh.wrap, function() error('foo', 0) end)
    assertFalse(ok5)
    assertEquals('function threw an error', tostring(result5))
    assertTrue(result5.traceback)
    assertEquals('foo', tostring(result5.nested_error))
    assertTrue(result5.nested_error.traceback)
end

LuaUnit:main()
