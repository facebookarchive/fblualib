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
local path = pl.path
local editline = require('fb.editline')
local completer = require('fb.editline.completer')
local eh = require('fb.util.error')
local base = require('fb.trepl.base')

-- k : name of variable
-- m : max length
local function printvar(key,val,m)
    local name = '[' .. tostring(key) .. ']'
    --io.write(name)
    name = name .. string.rep(' ',m-name:len()+2)
    local tp = type(val)
    if tp == 'userdata' then
        tp = torch.typename(val) or ''
        if tp:find('torch.*Tensor') or tp:find('torch.*Storage') then
            tp = base.sizestr(val, tp)
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

local error_handler = base.error_handler

-- Prompt:
local function make_repl(prompt_prefix)
    prompt_prefix = prompt_prefix or ''
    local c = base.with_color
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
                if not line then os.exit() end -- stdin was closed
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
                        print(eh.format(results[2]))
                    end
                    return 'error'
                end
            end

            local is_print  = (line == 'print' or line:find('^print[^%w_]'))
            local is_return = (line == 'return' or line:find('^return[^%w_]'))
            local is_stmt = (line:sub(-1) == ';')

            if not (is_print or is_return or is_stmt) then
                local test_func, _err = loadstring('return ' .. line)
                if test_func then
                    -- workaround bug in traceback for loadstring()
                    local inner_func, _err = loadstring('_RESULT={'..line..'}')
                    if inner_func then -- should always be true
                        local func = function() inner_func(); return unpack(_RESULT) end
                        timer_start()
                        return run_func(func, true)
                    end
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
