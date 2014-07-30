--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--
--

require('findc')

local M=function (byte_input)
    if byte_input:type()  ~= "torch.ByteTensor" then
        error("input should be 1d torch byte tensor")
    end
    if byte_input:dim() ~= 1 then
        error("input should be 1d torch byte tensor")
    end

    local N=byte_input:size()[1]
    local output=torch.zeros(N):long()
    local nnz=findc(output,byte_input)
    return output:sub(1, nnz)
end


return M
