--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

local pl = require('pl.import_into')()
local reactor = require('fb.util.reactor')

require('fb.luaunit')

function testRun()
    local callback_ran
    local r = reactor.Reactor()
    r:run(function()
        callback_ran = true
    end)
    assertTrue(not callback_ran)
    assertEquals(1, r:loop_nb())
    assertTrue(callback_ran)
end

function testRunAfterDelay()
    local callback_ran
    local r = reactor.Reactor()
    r:run_after_delay(0.05, function()
        callback_ran = true
    end)
    assertTrue(not callback_ran)
    assertEquals(1, r:loop())
    assertTrue(callback_ran)
end

function testLoopForever()
    local callback_count = 0
    local r = reactor.Reactor()
    local function callback()
        callback_count = callback_count + 1
        if callback_count ~= 5 then
            r:run(callback)
        end
    end
    r:run(callback)
    assertEquals(5, r:loop_nb())
    assertEquals(5, callback_count)
    assertEquals(0, r:loop_nb())
end

function testRemoveCallback()
    local r = reactor.Reactor()
    local id = r:run(function() error('should not run') end)
    r:remove_callback(id)
    assertEquals(0, r:loop_nb())
end

function testErrorPropagation()
    local r = reactor.Reactor()
    r:run(function() error('hello') end)
    local ok, err = pcall(function() r:loop_nb() end)
    assertFalse(ok)
    assertTrue(string.match(err, 'hello$'))
end

LuaUnit:main()
