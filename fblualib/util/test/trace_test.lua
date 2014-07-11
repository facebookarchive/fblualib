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
local trace = require('fb.util.trace')
local timing = require('fb.util.timing')

function testTrace()
    assertEquals({'foo', 'bar', 'baz', 'qux'},
                 trace.parse_key('foo:bar:baz:qux'))

    trace.namespace(
        'foo:bar',
        function()
            assertEquals({'foo', 'bar', 'baz', 'qux'},
                         trace.parse_key('::baz:qux'))
            assertEquals({'meow', '', 'quack', 'ribbit'},
                         trace.parse_key('meow::quack:ribbit'))
        end)

    local log = {}
    local function handler(key, args, nest)
        -- Remove time value for easy comparison
        local args_copy = pl.tablex.copy(args)
        assertTrue(args_copy.time)
        args_copy.time = nil
        table.insert(log, {trace.format_key(key), args_copy, nest})
    end

    -- everything under foo:ba.*, except foo:bar:baz
    trace.add_handler(handler, 'foo:bar:baz', true)
    trace.add_handler(handler, 'foo:ba.*')
    timing.add_pattern('foo:bar:baz', true)
    timing.add_pattern('foo:ba.*')
    timing.start('hello')

    trace.trace('foo:bar:x:y', {hello = 1})
    trace.trace('foo:bar:baz:meow', {world = 2})
    trace.trace('foo:bah:x:y', {goodbye = 3})
    trace.trace('foo:boo:x:y', {woot = 4})

    assertEquals({
        {'foo:bar:x:y', {hello = 1}, 0},
        {'foo:bah:x:y', {goodbye = 3}, 0},
    }, log)

    log = {}

    trace.trace_function(
        'foo:bar:x',
        function()
            trace.trace_function(
                '::y',
                function()
                    trace.trace(':::inside', {hello = 1})
                end,
                10, 20)
            return 1, 2
        end,
        30)

    assertEquals({
        {'foo:bar:x:entry', {args = {30}}, 1},
        {'foo:bar:y:entry', {args = {10, 20}}, 2},
        {'foo:bar:y:inside', {hello = 1}, 2},
        {'foo:bar:y:return', {ret = {}}, 2},
        {'foo:bar:x:return', {ret = {1, 2}}, 1},
    }, log)

    timing.finish()
end

LuaUnit:main()
