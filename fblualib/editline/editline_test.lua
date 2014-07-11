--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

local editline = require('fb.editline')
local completer = require('fb.editline.completer')

local el = editline.EditLine({
    prompt = 'hello> ',
    history_file=os.getenv('HOME') .. '/.foo_history',
    complete = completer.complete,
    auto_history = true,
})

while true do
    local line = el:read()
    if not line then
        break
    end
    print(line)
end
