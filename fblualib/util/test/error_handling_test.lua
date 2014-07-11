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

LuaUnit:main()
