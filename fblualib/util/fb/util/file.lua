-- nothing in lua's io library can truncate, so roll our own assuming
-- unix.
local ffi = require 'ffi'
local M = {}

-- man 2 truncate
ffi.cdef[[
int truncate(const char* path, size_t len);
int unlink(const char* pathname);
]]

M.truncate = ffi.C.truncate
M.unlink = ffi.C.unlink

return M
