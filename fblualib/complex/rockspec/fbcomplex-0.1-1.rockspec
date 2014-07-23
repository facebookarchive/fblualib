--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

package = 'fbcomplex'
version = '0.1-1'
source = {
    url = 'https://github.com/facebook/fblualib',
}
description = {
    summary = 'FB Lua Debugger',
    detailed = [[
    ]],
    homepage = 'https://github.com/facebook/fblualib',
    license = 'BSD',
}
supported_platforms = {
    'unix',
}
dependencies = {
    'penlight >= 1.3.1',
}
source = {
    url = 'https://github.com/facebook/fblualib',
    dir = 'fblualib/complex',
}
build = {
    type = 'builtin',
    modules = {
        ['fb.complex.init'] = 'fb/complex/init.lua',
    },
}
