--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

-- Logging facility, using google-glog for logging.
--
-- Similar to Python's logging. Usage:
--
-- local logging = require('fb.util.logging')
--
-- logging.info('Hello ', 42)  -- logs 'Hello 42' at INFO severity
-- logging.log(logging.INFO, 'Hello ', 42)  -- or specify explicitly
--
-- Also available are warn (to log with WARNING severity), error
-- (to log with ERROR severity), and fatal (to log with FATAL severity,
-- which will cause an immediate crash by calling abort()!)
--
-- There are also functions with the 'f' suffix (logf, infof, warnf, errorf,
-- fatalf) that process the first (or second, in the case of logf) argument
-- as a string.format string, and the remaining arguments as arguments to
-- the format string:
--
-- logging.infof('Hello %.2f!', 42)  -- logs 'Hello 42.00!' at INFO severity
-- logging.logf(logging.INFO, 'Hello %.2f!', 42)  -- same

local ffi = require('ffi')
local util = require('fb.util')
local clib = util._clib

local M = {}

local INFO = 0
local WARNING = 1
local ERROR = 2
-- Note that FATAL will crash your program by calling abort()!
local FATAL = 3
M.INFO = INFO
M.WARNING = WARNING
M.ERROR = ERROR
M.FATAL = FATAL

ffi.cdef([=[
void luaInitLogging(const char* argv0);
void luaLog(int severity, const char* file, int line, const char* msg);
]=])

local function _logm(depth, severity, msg)
    -- 0 = getinfo
    -- 1 = _log
    local info = debug.getinfo(depth + 1, 'nlS')

    local file = 'LUA_UNKNOWN'
    if info.source then
        local source_type = string.sub(info.source, 1, 1)
        local source = string.sub(info.source, 2)
        if source_type == '@' then  -- file
            file = source
        elseif source_type == '=' then  -- literal
            file = 'LUA_' .. string.gsub(source, '[^%w_%.%-]', '_')
        else
            file = 'LUA_EVAL'
        end
    end

    local line = info.currentline
    if not line or line <= 0 then
        line = 1
    end
    clib.luaLog(severity, file, line, msg)
end

local function _log(depth, severity, ...)
    local n = select('#', ...)
    local strings = {}
    for i = 1, n do
        table.insert(strings, tostring(select(i, ...)))
    end
    _logm(depth + 1, severity, table.concat(strings))
end

local function _logf(depth, severity, fmt, ...)
    _logm(depth + 1, severity, string.format(fmt, ...))
end

local function log(severity, ...)
    return _log(1, severity, ...)
end
M.log = log

local function logf(severity, ...)
    return _logf(1, severity, ...)
end
M.logf = logf

local function info(...)
    return _log(1, INFO, ...)
end
M.info = info

local function infof(...)
    return _logf(1, INFO, ...)
end
M.infof = infof

local function warn(...)
    return _log(1, WARNING, ...)
end
M.warn = warn
M.warning = warn

local function warnf(...)
    return _logf(1, WARNING, ...)
end
M.warnf = warnf
M.warningf = warnf

local function _error(...)
    return _log(1, ERROR, ...)
end
M.error = _error

local function errorf(...)
    return _logf(1, ERROR, ...)
end
M.errorf = errorf

local function fatal(...)
    return _log(1, FATAL, ...)
end
M.fatal = fatal

local function fatalf(...)
    return _logf(1, FATAL, ...)
end
M.fatalf = fatalf

-- Initialize logging library
clib.luaInitLogging('')

return M
