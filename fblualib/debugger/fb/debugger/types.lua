--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

local pl = require('pl.import_into')()
local utils = require('fb.debugger.utils')

local M = {}

local handled_types = {'table', 'userdata'}

local type_handlers = {}
for _, t in ipairs(handled_types) do
    type_handlers[t] = {}
end

local function verbose_type(obj)
    local t = type(obj)
    local handlers = type_handlers[t]

    if handlers then
        local mt = getmetatable(obj)
        for _, handler in ipairs(handlers) do
            local ok, result = pcall(handler, obj, mt)
            if ok and result then
                assert(type(result) == 'string')
                return result
            end
        end
    end

    return t
end
M.type = verbose_type

local function add_type_handler(t, handler)
    table.insert(type_handlers[t], handler)
end
M.add_type_handler = add_type_handler

-- default handlers

-- file
local function file_handler(obj, mt)
    local ft = io.type(obj)
    if ft then
        return ft
    end
end
add_type_handler('userdata', file_handler)

-- module
local function module_handler(obj)
    for k, v in pairs(package.loaded) do
        if v == obj then
            return 'module'
        end
    end
end
add_type_handler('table', module_handler)

-- Penlight class
local function pl_class_handler(obj, mt)
    if not mt then return end
    local cls = obj._class
    if cls.class_of then
        if cls == obj then
            if cls._name then
                return 'cls:' .. cls._name
            else
                return 'pl_class'
            end
        elseif cls == mt then
            if cls._name then
                return cls._name
            else
                return 'pl_object'
            end
        end
    end
end
add_type_handler('table', pl_class_handler)

local function empty_string()
    return ''
end

local function default_printer(obj, max_len)
    local more_suffix = '...'
    assert(max_len == nil or max_len >= #more_suffix)
    local s = tostring(obj)
    if #s > max_len then
        s = string.sub(s, 1, max_len - #more_suffix)
        s = s .. more_suffix
    end
    return s
end

local printers_for_type = {}

local function add_to_table(table, keys, value)
    if type(keys) == 'table' then
        for _, key in ipairs(keys) do
            table[key] = value
        end
    else
        table[keys] = value
    end
end

local function add_printer(types, printer)
    add_to_table(printers_for_type, types, printer)
end
M.add_printer = add_printer

local printers_for_pattern = {}

local function add_printer_for_pattern(patterns, printer)
    add_to_table(printers_for_pattern, patterns, printer)
end
M.add_printer_for_pattern = add_printer_for_pattern

-- don't truncate number representations
add_printer('number', tostring)
add_printer('string', utils.string_repr)
add_printer('boolean', tostring)

local function pretty_print(obj, max_len)
    local t = verbose_type(obj)
    local printer = printers_for_type[t]
    if printer then
        return printer(obj, max_len)
    end
    for pattern, printer in pairs(printers_for_pattern) do
        if string.match(t, pattern) then
            return printer(obj, max_len)
        end
    end
    return default_printer(obj, max_len)
end
M.pretty_print = pretty_print

-- Torch-specific code, if torch is installed
local ok, torch = pcall(require, 'torch')
if ok then
    -- Torch class
    local function torch_class_handler(obj, mt)
        if mt and obj.__typename then
            return obj.__typename
        end
    end
    add_type_handler('userdata', torch_class_handler)

    add_printer_for_pattern(
        {'^torch%.%a+Tensor$', '^torch%.%a+Storage$'},
        function(obj)
            local sz = obj:size():totable()
            if #sz == 0 then
                return 'empty'
            else
                return pl.stringx.join('x', sz)
            end
        end)
end

return M
