local util = require('fb.util')

local a = 0
a = a + util.import('.a')
a = a + util.import('.b')
a = a + util.import('.b.c')

return a
