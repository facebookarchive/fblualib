--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

local lib = require('fb.mattorch.lib')
local torch = require('torch')

local M = {}

-- load(path)
-- Load all vars in a Matlab file at path as a table of {name: tensor}
M.load = lib.load

-- save(path, vars, [ver])
-- Save all vars into a Matlab file at path.
--
-- If vars is a table, all values in the table are saved under their
-- corresponding names.
-- If vars is a tensor, it is saved under the uninspired name 'x'.
--
-- If given, ver must be a string indicating the Matlab file version to
-- generate: '4', '5', or '7.3'. The default is determined by the way
-- the underlying matio library was configured on your system, usually 5.
function M.save(path, vars, ver)
    if type(vars) == 'userdata' and torch.typename(vars) then
        lib.saveTensor(path, vars, ver)
    elseif type(vars) == 'table' then
        lib.saveTable(path, vars, ver)
    else
        error('Invalid vars type ' .. type(vars))
    end
end

return M
