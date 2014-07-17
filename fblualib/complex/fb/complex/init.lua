--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

local ffi = require('ffi')

local M = {}

local mt = {}
local complex

-- Ensure a number is of the complex type
local function tocomplex(a)
    if ffi.istype(complex, a) then
        return a
    elseif type(a) == 'number' then
        return complex(a, 0)
    else
        error('Invalid type, numeric type expected')
    end
end
M.tocomplex = tocomplex

-- Wrappers around <complex.h> functions
local one_operand = {
    'abs', 'acos', 'acosh', 'arg', 'asin', 'asinh', 'atan', 'atanh', 'cos',
    'cosh', 'exp', 'log', 'sin', 'sinh', 'sqrt', 'tan',
    'tanh',
}

local two_operand = {
    'pow',
}

for _, fn in ipairs(one_operand) do
    local cname = 'c' .. fn
    ffi.cdef(string.format('double _Complex %s(double _Complex)', cname))
    M[fn] = function(a)
        return ffi.C[cname](tocomplex(a))
    end
end

for _, fn in ipairs(two_operand) do
    local cname = 'c' .. fn
    ffi.cdef(string.format(
        'double _Complex %s(double _Complex, double _Complex)', cname))
    M[fn] = function(a, b)
        return ffi.C[cname](tocomplex(a), tocomplex(b))
    end
end

function M.conj(a)
    a = tocomplex(a)
    return complex(a.re, -a.im)
end

function mt.__add(a, b)
    a, b = tocomplex(a), tocomplex(b)
    return complex(a.re + b.re, a.im + b.im)
end

function mt.__sub(a, b)
    a, b = tocomplex(a), tocomplex(b)
    return complex(a.re - b.re, a.im - b.im)
end

function mt.__mul(a, b)
    a, b = tocomplex(a), tocomplex(b)
    return complex(a.re * b.re - a.im * b.im,
                   a.re * b.im + a.im * b.re)
end

function mt.__div(a, b)
    a, b = tocomplex(a), tocomplex(b)
    local d = b.re * b.re + b.im * b.im
    return complex((a.re * b.re + a.im * b.im) / d,
                   (a.im * b.re - a.re * b.im) / d)
end

function mt.__pow(a, b)
    a, b = tocomplex(a), tocomplex(b)
    return M.pow(a, b)
end

function mt.__unm(a)
    a = tocomplex(a)
    return complex(-a.re, -a.im)
end

complex = ffi.metatype('double _Complex', mt)

return M
