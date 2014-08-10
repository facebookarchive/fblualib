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
        r:terminate_loop()
    end)
    assertTrue(not callback_ran)
    r:loop()
    assertTrue(callback_ran)
end

function testRunAfterDelay()
    local callback_ran
    local r = reactor.Reactor()
    r:run_after_delay(0.05, function()
        callback_ran = true
        r:terminate_loop()
    end)
    assertTrue(not callback_ran)
    r:loop()
    assertTrue(callback_ran)
end

function testLoopForever()
    local callback_count = 0
    local r = reactor.Reactor()
    local function callback()
        callback_count = callback_count + 1
        if callback_count ~= 5 then
            r:run(callback)
        else
            r:terminate_loop()
        end
    end
    r:run(callback)
    assertEquals(0, callback_count)
    r:loop()
    assertEquals(5, callback_count)
end

LuaUnit:main()
