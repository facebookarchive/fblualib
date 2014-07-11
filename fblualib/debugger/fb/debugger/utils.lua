--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

local pl = require('pl.import_into')()
local printf = pl.utils.printf

local M = {}

local function plural(n, s, p)
    if n == 1 then
        return s
    elseif not p then
        return s .. 's'
    else
        return p
    end
end
M.plural = plural

local function print_numbered(str, b, context, current_line)
    if context then
        context = context - 1
    end
    local lines = pl.stringx.splitlines(str)

    if not b then
        b = 1
        if current_line and context then
            b = math.max(b, math.floor(current_line - context / 2))
        end
    end

    local e = #lines
    if context then
        e = math.min(e, b + context + 1)
    end

    for i = b, e do
        local line = lines[i]
        local line_marker = ' '
        if i == current_line then
            line_marker = '>'
        end
        printf('%5d %s %s\n', i, line_marker, line)
    end

    return e + 1
end
M.print_numbered = print_numbered

local translate_table = {
    [string.byte("'")] = "''",
    [string.byte('\\')] = '\\\\',
    [string.byte('\a')] = '\\a',
    [string.byte('\b')] = '\\b',
    [string.byte('\f')] = '\\f',
    [string.byte('\n')] = '\\n',
    [string.byte('\r')] = '\\r',
    [string.byte('\t')] = '\\t',
    [string.byte('\v')] = '\\v',
}

local function string_repr(s, max_len)
    local more_suffix = '...'
    if max_len then
        assert(max_len >= #more_suffix + 2)
        max_len = max_len - 2
    end
    local cut = false
    local out = {}
    local len = 0
    for i = 1, #s do
        local b = s:byte(i)
        local t = translate_table[b]
        if not t then
            if b >= 32 and b <= 126 then
                t = string.char(b)
            else
                t = string.format('\\%03d', b)
            end
        end
        table.insert(out, t)
        len = len + #t
        if max_len and len > max_len then
            while len + #more_suffix > max_len do
                t = out[#out]
                table.remove(out)
                len = len - #t
            end
            cut = true
            break
        end
    end
    local out_str = "'" .. pl.stringx.join('', out) .. "'"
    if cut then
        out_str = out_str .. more_suffix
    end
    return out_str
end
M.string_repr = string_repr

return M
