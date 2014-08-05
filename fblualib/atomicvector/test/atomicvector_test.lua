--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

require('fb.luaunit')

function testSmoke()
    local av = require('fb.atomicvector')
    require('torch')

    local name = "foobert" .. math.random(320)
    local succ = av.create_double(name)
    local vec = av.get(name)
    assertEquals(succ, true)
    assertEquals(#vec, 0)

    -- Table-like operations?
    local tensor = torch.randn(12, 10)
    av.append(vec, tensor)
    assertEquals(#vec, 1)
    local t2 = vec[1]
    assertEquals(t2, tensor)

    -- Overwrite.
    local t3 = torch.randn(32)
    local t3copy = t3
    vec[1] = t3
    assertEquals(#vec, 1)
    assertEquals(vec[1], t3copy)
    assertEquals(vec[1], t3)

    -- Append via write
    local t4 = torch.randn(17)
    local t4copy = t4
    vec[2] = t4
    assertEquals(#vec, 2)
    assertEquals(vec[1], t3)
    assertEquals(vec[1], t3copy)
    assertEquals(t4, vec[2])

    vec = nil
    av.destroy(name)
    collectgarbage()
end

function testErrorClib()
    local av = require('fb.atomicvector')

    local name = "foobert" .. math.random(111)
    av.create_double(name)
    local vec = av.get(name)
    local success, message = pcall(function()
        av.append(vec, torch.randn(3):float())
    end)
    assertEquals(success, false)
    vec = nil
    av.destroy(name)

    av.create_float(name)
    local vec = av.get(name)
    local success, message = pcall(function()
        av.append(vec, torch.randn(3))
    end)
    assertEquals(success, false)
    vec = nil
    collectgarbage()
end

function testGetError()
    local av = require('fb.atomicvector')
    local rslt, no_such_vec = pcall(function() av.get('nopes') end)
    assert(not rslt)
end

function testAppendError()
    local av = require('fb.atomicvector')
    local no_such_vec = nil
    local rslt, msg = pcall(function()
        av.append(no_such_vec, torch.randn(12))
    end)
    assert(not rslt)
end

function testParallelAppend()
    local threads = require('threads')
    local numThreads = 40
    local numTensorsPerThread = 4096
    local itemsPerThread = 10000
    local av = require 'fb.atomicvector'

    -- Each thread sticks in a 2-element tensor: [tid][itemid]
    -- Invariants:
    --    All threads inserted all of their elements.
    --    They all appear in order.

    local name = "shared" .. math.random(3200)
    local succ = av.create_double(name)
    assert(succ)

    local function dprint(...)
         if os.getenv("DBG") then
             print(...)
         end
    end

    local workers = threads(numThreads,
        function(threadidx)
            sdl = require 'sdl2'
        end,
        function(threadidx)
            -- Capture stuff we need in the thread body. This is bizarre;
            -- some locals get magically captured, others do not.
            g_name = name
            g_numTensorsPerThread = numTensorsPerThread
        end)

    for i = 1, numThreads do
        workers:addjob(function()
            local av = require 'fb.atomicvector'
            local vec = av.get(g_name)
            assert(vec ~= nil)
            assert(type(vec) == 'userdata')
            for j = 1, g_numTensorsPerThread do
                av.append(vec, torch.Tensor({i, j}))
            end
        end)
    end
    workers:synchronize()

    vec = av.get(name)
    assert(#vec == numThreads * numTensorsPerThread)
    threadToVecs = { }
    for i = 1, numThreads do
        threadToVecs[i] = { }
    end
    for vi = 1, #vec do
        local t = vec[vi]
        assertEquals(torch.typename(t), "torch.DoubleTensor")
        assertEquals(#t:size(), 1)
        assertEquals(t:size()[1], 2)
        -- The order is not predictable! But each thread's
        -- should appear in order.
        local tensorTidx = t[1]
        local tensorOrder = t[2]
        assert(t[1] >= 1)
        assert(t[1] <= numThreads)
        if tensorOrder ~= #threadToVecs[tensorTidx] + 1 then
            print ("inconsistency! thread,tensor",  tensorTidx, tensorOrder )
            for i = 1, #threadToVecs[tensorTidx] do
                print (i, #threadToVecs[tensorTidx][i])
            end
        end
        assertEquals(tensorOrder, #threadToVecs[tensorTidx] + 1)
        table.insert(threadToVecs[tensorTidx], t)
    end
    vec = nil
    av.destroy(name)
end


LuaUnit:main()
