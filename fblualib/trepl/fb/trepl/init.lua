--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

--[============================================================================[
REPL: A REPL for Lua (with support for Torch objects).

Support for SHELL commands:
> $ ls
> $ ll
> $ ls -l
(prepend any command by $, from the Lua interpreter)

Copyright: MIT / BSD / Do whatever you want with it.
Clement Farabet, 2013
--]============================================================================]

-- Require Torch
pcall(require,'torch')
pcall(require,'paths')

local pl = require('pl.import_into')()
local editline = require('fb.editline')
local completer = require('fb.editline.completer')
local eh = require('fb.util.error')

local printf = pl.utils.printf

-- Colors:
local colors = {
    none = '\27[0m',
    black = '\27[0;30m',
    red = '\27[0;31m',
    green = '\27[0;32m',
    yellow = '\27[0;33m',
    blue = '\27[0;34m',
    magenta = '\27[0;35m',
    cyan = '\27[0;36m',
    white = '\27[0;37m',
    Black = '\27[0;1;30m',
    Red = '\27[0;1;31m',
    Green = '\27[0;1;32m',
    Yellow = '\27[0;1;33m',
    Blue = '\27[0;1;34m',
    Magenta = '\27[0;1;35m',
    Cyan = '\27[0;1;36m',
    White = '\27[0;1;37m',
    Default = '\27[0;1m',
    _black = '\27[40m',
    _red = '\27[41m',
    _green = '\27[42m',
    _yellow = '\27[43m',
    _blue = '\27[44m',
    _magenta = '\27[45m',
    _cyan = '\27[46m',
    _white = '\27[47m',
    _default = '\27[49m',
}


-- Apply:
local c
local use_colors
local color_mode = os.getenv('TORCH_COLOR')
if color_mode == 'always' then
    use_colors = true
elseif color_mode ~= 'never' then
    use_colors = editline.tty
end

if use_colors then
    c = function(color, txt)
        return colors[color] .. txt .. colors.none
    end
else
    c = function(color,txt) return txt end
end

-- If no Torch:
if not torch then
    torch = {
        typename = function() return '' end
    }
end

-- helper
local function sizestr(x)
    local strt = {}
    if _G.torch.typename(x):find('torch.*Storage') then
        return _G.torch.typename(x):match('torch%.(.+)') .. ' - size: ' .. x:size()
    end
    if x:nDimension() == 0 then
        table.insert(strt, _G.torch.typename(x):match('torch%.(.+)') .. ' - empty')
    else
        table.insert(strt, _G.torch.typename(x):match('torch%.(.+)') .. ' - size: ')
        for i=1,x:nDimension() do
            table.insert(strt, x:size(i))
            if i ~= x:nDimension() then
                table.insert(strt, 'x')
            end
        end
    end
    return table.concat(strt)
end

-- k : name of variable
-- m : max length
local function printvar(key,val,m)
    local name = '[' .. tostring(key) .. ']'
    --io.write(name)
    name = name .. string.rep(' ',m-name:len()+2)
    local tp = type(val)
    if tp == 'userdata' then
        tp = torch.typename(val) or ''
        if tp:find('torch.*Tensor') then
            tp = sizestr(val)
        elseif tp:find('torch.*Storage') then
            tp = sizestr(val)
        else
            tp = tostring(val)
        end
    elseif tp == 'table' then
        tp = tp .. ' - size: ' .. #val
    elseif tp == 'string' then
        local tostr = val:gsub('\n','\\n')
        if #tostr>40 then
            tostr = tostr:sub(1,40) .. '...'
        end
        tp = tp .. ' : "' .. tostr .. '"'
    else
        tp = tostring(val)
    end
    return name .. ' = ' .. tp
end

-- helper
local function getmaxlen(vars)
    local m = 0
    if type(vars) ~= 'table' then return tostring(vars):len() end
    for k,v in pairs(vars) do
        local s = tostring(k)
        if s:len() > m then
            m = s:len()
        end
    end
    return m
end

-- overload print:
if not print_old then
    print_old=print
end

-- a function to colorize output:
local function colorize(object,nested)
    -- Apply:
    local apply = c

    -- Type?
    if object == nil then
        return apply('Default', 'nil')
    elseif type(object) == 'number' then
        return apply('cyan', tostring(object))
    elseif type(object) == 'boolean' then
        return apply('blue', tostring(object))
    elseif type(object) == 'string' then
        if nested then
            return apply('Default','"')..apply('green', object)..apply('Default','"')
        else
            return apply('none', object)
        end
    elseif type(object) == 'function' then
        return apply('magenta', tostring(object))
    elseif type(object) == 'userdata' or type(object) == 'cdata' then
        local tp = torch.typename(object) or ''
        if tp:find('torch.*Tensor') then
            tp = sizestr(object)
        elseif tp:find('torch.*Storage') then
            tp = sizestr(object)
        else
            tp = tostring(object)
        end
        if tp ~= '' then
            return apply('red', tp)
        else
            return apply('red', tostring(object))
        end
    elseif type(object) == 'table' then
        return apply('green', tostring(object))
    else
        return apply('none', tostring(object))
    end
end

-- This is a new recursive, colored print.
local ndepth = 4
function print_new(...)
    local function rawprint(o)
        io.write(tostring(o or '') .. '\n')
        io.flush()
    end
    local objs = {...}
    local function printrecursive(obj,depth)
        local depth = depth or 0
        local tab = depth*4
        local line = function(s) for i=1,tab do io.write(' ') end rawprint(s) end
        line('{')
        tab = tab+2
        for k,v in pairs(obj) do
            if type(v) == 'table' then
                if depth >= (ndepth-1) or next(v) == nil then
                    line(tostring(k) .. ' : ' .. colorize(v,true))
                else
                    line(tostring(k) .. ' : ') printrecursive(v,depth+1)
                end
            else
                line(tostring(k) .. ' : ' .. colorize(v,true))
            end
        end
        tab = tab-2
        line('}')
    end
    for i = 1,select('#',...) do
        local obj = select(i,...)
        if type(obj) ~= 'table' then
            if type(obj) == 'userdata' or type(obj) == 'cdata' then
                rawprint(obj)
            else
                io.write(colorize(obj) .. '\t')
                if i == select('#',...) then
                    rawprint()
                end
            end
        elseif getmetatable(obj) and getmetatable(obj).__tostring then
            rawprint(obj)
            --printrecursive(obj)
        else
            printrecursive(obj)
        end
    end
end


function setprintlevel(n)
    if n == nil or n < 0 then
        error('expected number [0,+)')
    end
    n = math.floor(n)
    ndepth = n
    if ndepth == 0 then
        print = print_old
    else
        print = print_new
    end
end
setprintlevel(5)

-- Import, ala Python
function import(package, forced)
    local ret = require(package)
    local symbols = {}
    if _G[package] then
        _G._torchimport = _G._torchimport or {}
        _G._torchimport[package] = _G[package]
        symbols = _G[package]
    elseif ret and type(ret) == 'table' then
        _G._torchimport = _G._torchimport or {}
        _G._torchimport[package] = ret
        symbols = ret
    end
    for k,v in pairs(symbols) do
        if not _G[k] or forced then
            _G[k] = v
        end
    end
end

-- Smarter require (ala Node.js)
local drequire = require
function require(name)
    if name:find('^%.') then
        local file = debug.getinfo(2).source:gsub('^@','')
        local dir = '.'
        if path.exists(file) then
            dir = path.dirname(file)
        end
        local pkgpath = path.join(dir,name)
        if path.isfile(pkgpath..'.lua') then
            return dofile(pkgpath..'.lua')
        elseif path.isfile(pkgpath) then
            return dofile(pkgpath)
        elseif path.isfile(pkgpath..'.so') then
            return package.loadlib(pkgpath..'.so', 'luaopen_'..path.basename(name))()
        elseif path.isfile(pkgpath..'.dylib') then
            return package.loadlib(pkgpath..'.dylib', 'luaopen_'..path.basename(name))()
        else
            local initpath = path.join(pkgpath,'init.lua')
            return dofile(initpath)
        end
    else
        return drequire(name)
    end
end

-- Who
-- a simple function that prints all the symbols defined by the user
-- very much like Matlab's who function
function who(system)
    local m = getmaxlen(_G)
    local p = _G._preloaded_
    local function printsymb(sys)
        for k,v in pairs(_G) do
            if (sys and p[k]) or (not sys and not p[k]) then
                print(printvar(k,_G[k],m))
            end
        end
    end
    if system then
        print('== System Variables ==')
        printsymb(true)
    end
    print('== User Variables ==')
    printsymb(false)
    print('==')
end

-- Monitor Globals
function monitor_G(cb)
    local evercreated = {}
    for k in pairs(_G) do
        evercreated[k] = true
    end
    setmetatable(_G, {
        __newindex = function(G,key,val)
            if not evercreated[key] then
                if cb then
                    cb(key)
                else
                    print('created a global variable: ' .. key)
                end
            end
            evercreated[key] = true
            rawset(G,key,val)
        end
    })
end

-- Tracekback (error printout)
local function traceback(message)
    local tp = type(message)
    if tp ~= "string" and tp ~= "number" then return message end
    local debug = _G.debug
    if type(debug) ~= "table" then return message end
    local tb = debug.traceback
    if type(tb) ~= "function" then return message end
    return tb(message)
end

local error_handler = traceback

if os.getenv('LUA_DEBUG_ON_ERROR') then
    local debugger = require('fb.debugger')
    error_handler = function(message)
        local tb = traceback(message)
        print(tb)
        debugger.enter()
    end
    debugger.add_skip_func(error_handler)
end

-- Prompt:
local function make_repl(prompt_prefix)
    prompt_prefix = prompt_prefix or ''
    local function prompt(aux)
        local s
        if not aux then
            s = '> '
        else
            s = '>> '
        end
        return prompt_prefix .. s
    end

    -- Reults:
    _RESULTS = {}
    _LAST = ''

    local multiline_chunk
    local el = editline.EditLine({
        prompt_func = function() return prompt(multiline_chunk) end,
        history_file = os.getenv('HOME') .. '/.luahistory',
        complete = completer.complete,
    })

    local function repl()
        -- Timer
        local timer_start, timer_stop
        if torch and torch.Timer then
            local t = torch.Timer()
            local start = 0
            timer_start = function()
                start = t:time().real
            end
            timer_stop = function()
                local step = t:time().real - start
                for i = 1,70 do io.write(' ') end
                print(c('Default',string.format('[%0.04fs]', step)))
            end
        else
            timer_start = function() end
            timer_stop = function() end
        end

        local function process_statement(line)
            if not line or line == 'exit' or line == 'quit' then
                io.stdout:write('Do you really want to exit ([y]/n)? ')
                io.flush()
                local line = io.read('*l')
                if line == '' or line:lower() == 'y' then
                    return 'quit'
                end
                return 'ok'
            end

            if line:sub(1, 1) == '=' then
                line = 'return ' .. line:sub(2)
            end

            -- OS Commands:
            local first_char = line:sub(1, 1)
            if first_char == '!' or first_char == '$' then
                local cline = line:sub(2)
                timer_start()
                local f = io.popen(cline)
                local res = f:read('*a')
                f:close()
                io.write(c('none',res)) io.flush()
                table.insert(_RESULTS, res)
                _LAST = _RESULTS[#_RESULTS]
                timer_stop()
                return 'ok'
            end

            -- shortcut to get help; TODO(tudorb) doesn't work
            if first_char == '?' then
                local ok = pcall(require,'dok')
                if ok then
                    line = 'help(' .. line:sub(2) .. ')'
                else
                    print('error: could not load help backend')
                    return 'error'
                end
            end

            local run_func = function(func, print_results)
                local results = {xpcall(func, error_handler)}
                if results[1] then  -- success!
                    if print_results then
                        table.insert(_RESULTS, results[2])
                        if #results == 1 then
                            print(nil)
                        else
                            print(unpack(results, 2))
                        end
                    end
                    timer_stop()
                    return 'ok'
                else
                    if results[2] then
                        print(results[2])
                    end
                    return 'error'
                end
            end

            local is_print  = (line == 'print' or line:find('^print[^%w_]'))
            local is_return = (line == 'return' or line:find('^return[^%w_]'))
            local is_stmt = (line:sub(-1) == ';')

            if not (is_print or is_return or is_stmt) then
                local func, err = loadstring('return ' .. line)
                if func then
                    timer_start()
                    return run_func(func, true)
                end
            end

            local func, err = loadstring(line)
            if func then
                timer_start()
                -- only print results for expressions of the form
                -- 'return X'
                return run_func(func, is_return)
            end

            if err:sub(-7) == "'<eof>'" then
                return 'continue'
            else
                print(err)
                return 'error'
            end
        end

        local function process_line(line)
            if line then
                if line:find('^%s*$') then
                    return true
                end
                if multiline_chunk then
                    line = multiline_chunk .. line
                    multiline_chunk = nil
                end
            end

            local stripped_line
            if line then
                stripped_line = pl.stringx.strip(line)
            end

            local res = process_statement(stripped_line)

            if res == 'quit' then
                return false
            end

            if res == 'continue' then
                assert(line)
                multiline_chunk = line
            else
                el:add_history(line)
            end
            return true
        end

        while true do
            local line = el:read()
            if not process_line(line) then
                break
            end
        end
    end

    return repl
end


-- Store preloaded symbols, for who()
_G._preloaded_ = {}
for k,v in pairs(_G) do
    _G._preloaded_[k] = true
end

local function repl()
    make_repl()()
end

return {
    make_repl = make_repl,
    repl = repl,
    error_handler = error_handler,
}
