--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

local pl = require('pl.import_into')()
local M = {}

-- Facility for conditional debugg output. Controlled via the 'DBG' environment
-- variable, which is a comma-separated list of name=<threshold> pairs.
-- Typical use might be:
--
--     local dbg = require 'dbg'
--     local dprint = dbg.new('myModuleName')
--
--     ...
--     dprint('Initializing...\n")  -- Prints if DBG=myModuleName=<any val>
--     dprint(2, 'Reticulating splines: ', numSplines)

function M.new(mod)
    return function(level, ...)
        if M.dbgSettings[mod] and M.dbgSettings[mod] >= level then
            print(...)
        end
    end
end

local function parse_debug_str(str)
    local retval = { }
    if not str then
        return retval
    end
    local chunks = pl.utils.split(str, ',')
    for i, chunk in ipairs(chunks) do
        local pair = pl.utils.split(chunk, '=')
        if #pair == 1 then
            -- If the user didn't specify a level, turn on all statements
            -- for this module.
            retval[pair[1]] = 1e8
        elseif #pair == 2 then
            retval[pair[1]] = tonumber(pair[2])
        else
            error("bad DBG segment: " .. chunk)
        end
    end
    return retval
end

local success, settings = pcall(function()
    return parse_debug_str(os.getenv('DBG'))
end)

if success then
    M.dbgSettings = settings
else
    print("failed to parse DBG env var", os.getenv('DBG'))
end
-- Use the 'dbgmeta' module to debug dbg.
local dprint = M.new('fb.util.dbg')

dprint(1, "dbg settings", M.dbgSettings)

return M
