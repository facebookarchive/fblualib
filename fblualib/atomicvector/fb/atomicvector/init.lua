--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

local clib = require('fb.atomicvector.clib')
local torch = require('torch')

local M = {
    create_float = clib.create_float,
    create_double = clib.create_double,
    get = clib.get,
    destroy = clib.destroy,
    append = clib.append
}

-- save() takes an atomic vector and a Torch DiskFile as inputs
function M.save(atom_vec, diskfile)
    diskfile:writeObject(#atom_vec)
    for i = 1,#atom_vec do
        diskfile:writeObject(atom_vec[i])
    end
end

-- load() takes a filename and an atomic vector as inputs
function M.load(filename, atom_vec)
    local f = torch.DiskFile(filename, 'r')
    local vec_sz = f:readObject()
    for i = 1, vec_sz do
        local t = f:readObject()
        clib.append(atom_vec, t)
    end
    f:close()
end

return M
