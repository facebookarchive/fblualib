--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

package = 'fbluaunit'
version = '0.1-1'
source = {
    url = 'https://github.com/facebook/fblualib',
}
description = {
    summary = 'FB Lua Debugger',
    detailed = [[
      FB fork of LuaUnit.
    ]],
    homepage = 'https://github.com/facebook/fblualib',
    license = 'BSD',
}
supported_platforms = {
    'unix',
}
dependencies = {
    'lua-cjson >= 2.1.0',
}
source = {
    url = 'https://github.com/facebook/fblualib',
    dir = 'fblualib/fbluaunit',
}
build = {
    type = 'builtin',
    modules = {
	['fb.luaunit.init'] = 'fb/luaunit/init.lua',
    },
}
