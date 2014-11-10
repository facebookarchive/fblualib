--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

require 'fb.luaunit'

local file = require 'fb.util.file'

function testTrunc()
    local filename = os.tmpname()
    local f = io.open(filename, 'w')
    for i = 1, 1024 do
        f:write("w")
    end
    f:close()

    file.truncate(filename, 0)
    f = io.open(filename, 'r')
    local size = f:seek('end')
    f:close()
    assertEquals(size, 0)

    file.unlink(filename)
end

testTrunc()
LuaUnit:main()
