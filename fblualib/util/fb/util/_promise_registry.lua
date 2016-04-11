--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

-- Internal module for the use of fblualib/Promise.h

local future = require('fb.util.future')
local M = {}

local id = 0  -- 2**53 values ought to be enough for everybody

local promises = {}

function M.create(...)
    id = id + 1
    local p = {future.Promise(), ...}
    promises[id] = p
    return p[1]:future(), id
end

local function dispatch(k, method, ...)
    local p = promises[k][1]
    p[method](p, ...)
    promises[k] = nil
end

function M.set_value(k, ...)
    return dispatch(k, 'set_value', ...)
end

function M.set_error(k, e)
    return dispatch(k, 'set_error', e)
end

return M
