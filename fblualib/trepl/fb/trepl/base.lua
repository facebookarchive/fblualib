local eh = require('fb.util.error')

local M = {}

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
local use_colors
local color_mode = os.getenv('TORCH_COLOR')
if color_mode == 'always' then
    use_colors = true
elseif color_mode ~= 'never' then
    local ffi = require 'ffi'
    ffi.cdef [[ int isatty(int); ]]
    use_colors = (ffi.C.isatty(0) == 1) and (ffi.C.isatty(1) == 1)
end

local function with_color(color, txt)
    if use_colors then
        return colors[color] .. txt .. colors.none
    else
        return txt
    end
end

local function sizestr(x, typename)
    if typename:find('torch.*Storage') then
        return typename:match('torch%.(.+)') .. ' - size: ' .. x:size()
    end
    if x:nDimension() == 0 then
        return typename:match('torch%.(.+)') .. ' - empty'
    end
    local strt = {}
    table.insert(strt, typename:match('torch%.(.+)') .. ' - size: ')
    for i=1,x:nDimension() do
        table.insert(strt, x:size(i))
        if i ~= x:nDimension() then
            table.insert(strt, 'x')
        end
    end
    return table.concat(strt)
end

-- a function to colorize output:
local function colorize(object,nested)
    -- Apply:
    local apply = with_color

    -- Type?
    if object == nil then
        return apply('Default', 'nil')
    elseif type(object) == 'number' then
        return apply('cyan', tostring(object))
    elseif type(object) == 'boolean' then
        return apply('blue', tostring(object))
    elseif type(object) == 'string' then
        if nested then
            return apply('Default','"') .. apply('green', object) ..
                apply('Default','"')
        else
            return apply('none', object)
        end
    elseif type(object) == 'function' then
        return apply('magenta', tostring(object))
    elseif type(object) == 'userdata' or type(object) == 'cdata' then
        local tp = ''
        if torch and torch.typename then
            tp = torch.typename(object) or ''
        end
        if tp:find('torch.*Tensor') or tp:find('torch.*Storage') then
            tp = sizestr(object, tp)
        else
            tp = tostring(object)
        end
        return apply('red', tp)
    elseif type(object) == 'table' then
        return apply('green', tostring(object))
    else
        return apply('none', tostring(object))
    end
end

-- This is a new recursive, colored print.
local ndepth = 5
local function print_new(...)
    local function rawprint(o)
        io.write(tostring(o or '') .. '\n')
        io.flush()
    end
    local function printrecursive(obj,depth)
        local depth = depth or 0
        local tab = depth*4
        local line = function(s)
            for i=1,tab do io.write(' ') end
            rawprint(s)
        end
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

-- Traceback (error printout)
local function traceback(message)
    local tp = type(message)
    if tp ~= "string" and tp ~= "number" then return message end
    local debug = _G.debug
    if type(debug) ~= "table" then return message end
    local tb = debug.traceback
    if type(tb) ~= "function" then return message end
    return tb(coroutine.running(), message)
end

local error_handler = eh.wrap

if os.getenv('LUA_DEBUG_ON_ERROR') then
    local debugger = require('fb.debugger')
    error_handler = function(message)
        local tb = traceback(message)
        print(tb)
        debugger.enter()
    end
    debugger.add_skip_func(error_handler)
end

M.print = print_new
M.error_handler = error_handler
M.traceback = traceback
M.with_color = with_color
M.sizestr = sizestr

function M.exec(package)
    _G.print = print_new
    local ok, err = xpcall(require, error_handler, package)
    if not ok then
        io.stderr:write(eh.format(err) .. '\n')
        os.exit(1)
    else
        os.exit(0)
    end
end

return M
