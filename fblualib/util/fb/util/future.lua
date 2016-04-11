--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

-- Futures library, similar to Scala futures
--
-- There are two important concepts: Futures and Promises.
--
-- A Promise object represents just that -- a promise that you will perform
-- a computation at some later stage.
--
-- A Future represents the (anticipated) result of a computation that hasn't
-- yet completed.
--
-- Promises and Futures are the two sides of a producer/consumer relationship:
-- holders of Promises produce results, while holders of Futures consume them.
--
-- A Future can be in "pending" state while the computation hasn't completed,
-- or it can hold a value (the result of the computation) or an error (if the
-- computation failed).
--
-- You may set three kinds of callbacks on a future:
--
-- - success callbacks (on_success), which will be called when the future
--   completes successfully; the argument is the value stored in the future
--
-- - error callbacks (on_error), which will be called when the future
--   completes with an error; the argument is the error stored in the future
--
-- - completion callbacks (on_completion), which will be called when
--   the future completes (either successfully or with an error); the
--   argument is the future itself.
--
-- If the future was already completed when you set a relevant callback,
-- the callback will be called immediately.
--
-- When the future completes, all relevant callbacks will be called immediately
-- at the point of the call to the promise's set_value / set_error.
--
-- If a callback throws an exception, the other relevant callbacks will
-- still be called, the future will still complete (successfully or
-- with an error), but the relevant function (on_* / set_*) will raise an
-- error.
--
-- The power of futures comes from being able to combine them. Note that
-- all combinators are implemented in terms of on_success / on_error /
-- on_completion:
--
-- f:map(func)
--   returns a new future (g) that maps the (successful) result of f through
--   func. That is, g completes as follows:
--   - when f completes with an error, g completes with the same error
--   - when f completes successfully, g completes successfully with the
--     value of func(f:value()) (unless func throws an error, in which
--     case g completes with the same error)
--
-- f:flat_map(func)
--   "chains" a future through an async function (that retunrs a future).
--   That is, once f, completes successfully, its value is passed through
--   a function which returns a future.
--   - when f completes with an error, g completes with the same error
--   - when f completes successfuly, we call func(f:value()), which returns
--     a new future h. g completes the same way as h (unless func throws an
--     error, in which case g completes with the same error)
--
-- f:and_then(func)
--   returns a new future (g) that calls func on the successful value of f,
--   and then returns the same value as f. That is, func will be called
--   for side effects only iff f completes successfully:
--   - when f completes with an error, g completes with the same error
--   - when f completes successfully, we call func(f:value()), and g
--     completes successfully with f:value() (unless func throws an error,
--     in which case g completes with the same error)
--
-- f:enforce(func)
--   returns a new future (g) that completes successfully with f:value()
--   iff f completes successfully and func(f:value()) evaluates as true
--
-- all(f1, f2, ...)
--   returns a new future (g) that completes successfully after all f_i
--   complete successfully with multiple return values:
--   f1:value(), f2:value(), .... That is:
--   - when any of f_i completes with an error, g completes with the same
--     error
--   - when all f_i complete successfully, g completes successfully with
--     f1:value(), f2:value(), ...
--   Note that this only collects the first result from each future, in case
--   they return multiple results; see all_multi below.
--
-- all_multi(f1, f2, ...)
--   returns a new future (g) that completes successfully after all f_i
--   complete successfully with multiple return values, each being a list:
--   {f1:value()...}, {f2:value()...}, .... That is:
--   - when any of f_i completes with an error, g completes with the same
--     error
--   - when all f_i complete successfully, g completes successfully with
--     {f1:value()...}, {f2:value()...}, ...
--   Each list collects all results returned by the corresponding future.
--   As some results may be nil, those lists also have a field n that indicates
--   the number of results, just like _G.arg.
--
-- any_index(f1, f2, ...)
--   returns a new future (g) that completes successfuly after any one of
--   f_i completes successfully; the completion value is i, f_i:value().
--   That is:
--   - when any of f_i completes with an error, g completes with the same
--     error
--   - when any of f_i completes successfully, g completes successfully with
--     i, f_i:value()
--
-- any(f1, f2, ...)
--   similar to any_index, except that the index is not returned; when
--   f_i completes successfully, the completion value is f_i:value()
--
-- reduce_index(func, iv, f1, f2, ...)
--   returns a new future g that completes successfully after all of f_i
--   complete successfully; the completion value is obtained by reducing f_i
--   through func *in an unspecified order* with the given zero value.
--   This version also passes the index of the future that completes;
--   see reduce(), below.
--   That is, consider an value A, initialized to iv:
--   - when any of f_i completes with an error, g completes with the same
--     error
--   - when a f_i completes successfully, we set A = func(A, i, f_i:value())
--   - when *all* f_i completes successfuly, g completes with the value A
--
-- reduce(func, iv, f1, f2, ...)
--   returns a new future g that completes successfully after all of f_i
--   complete successfully; the completion value is obtained by reducing f_i
--   through func *in an unspecified order* with the given zero value.
--   That is, consider an value A, initialized to iv:
--   - when any of f_i completes with an error, g completes with the same
--     error
--   - when a f_i completes successfully, we set A = func(A, f_i:value())
--   - when *all* f_i completes successfuly, g completes with the value A
--

local pl = require('pl.import_into')()
local util = require('fb.util')
local eh = util.import('.error')

local M = {}

local _SUCCESS = {'success'}
local _ERROR = {'error'}
local _PENDING = {'pending'}

local Future = pl.class()

-- Constructor. For internal use only. You can't call it directly.
function Future:_init(mode, ...)
    assert(mode == _SUCCESS or mode == _ERROR or mode == _PENDING)
    self._mode = mode
    self._result = util.pack_n(...)
    self._callbacks = {[_SUCCESS] = {}, [_ERROR] = {}}
    self._completion_callbacks = {}
end

-- Return the mode (one of 'success', 'error', 'pending')
function Future:mode()
    return self._mode[1]
end

-- Return true iff still pending (not completed)
function Future:is_pending()
    return self._mode == _PENDING
end

-- Return true iff has value (successful)
function Future:has_value()
    return self._mode == _SUCCESS
end

-- Return the value if it has a value, reraise the error if it has an error;
-- error if still pending.
function Future:value()
    if self:has_value() then
        return util.unpack_n(self._result)
    end
    if self:has_error() then
        error(self._result[1])
    end
    error('Future has no value (pending)')
end

-- Return true iff has error (failed)
function Future:has_error()
    return self._mode == _ERROR
end

-- Return the error
function Future:error()
    assert(self:has_error(), 'Future has no error (' .. self:mode() .. ')')
    return self._result[1]
end

-- Add callback to be executed on successful completion
function Future:on_success(cb)
    return self:_add_callback(_SUCCESS, cb)
end

-- Add callback to be executed on error completion
function Future:on_error(cb)
    return self:_add_callback(_ERROR, cb)
end

-- Add callback for either success / error
function Future:_add_callback(mode, cb)
    if self._mode == _PENDING then
        -- Still pending, add to list
        table.insert(self._callbacks[mode], cb)
    elseif self._mode == mode then
        -- Execute immediately
        local ok, err = self:_call(cb, mode[1], util.unpack_n(self._result))
        if not ok then
            error(err)
        end
    end
    return self
end

-- Add callback to be executed on completion
function Future:on_completion(cb)
    if self._mode == _PENDING then
        -- Still pending, add to list
        table.insert(self._completion_callbacks, cb)
    else
        -- Execute immediately
        local ok, err = self:_call(cb, 'completion', self)
        if not ok then
            error(err)
        end
    end
    return self
end

function Future:_complete(mode, ...)
    assert(self:is_pending(),
           'Future already completed (' .. self:mode() .. ')')
    self._mode = mode
    self._result = util.pack_n(...)

    local all_ok = true
    local all_err = ''

    -- Run success/failure callbacks
    local ok, err =
        self:_call_all(self._callbacks[mode], mode[1],
                       util.unpack_n(self._result))
    if not ok then
        all_ok = false
        all_err = all_err .. err
    end

    -- Run completion callbacks
    ok, err = self:_call_all(self._completion_callbacks, 'completion', self)
    if not ok then
        all_ok = false
        all_err = all_err .. err
    end

    -- Let the callbacks be GCed
    self._callbacks = nil
    self._completion_callbacks = nil

    -- Rethrow if necessary
    if not all_ok then
        error(all_err)
    end
end

-- Call one callback. Callbacks aren't supposed to throw; be loud.
function Future:_call(cb, message, ...)
    local function error_handler(err)
        return eh.create(message .. ' callback raised an error', eh.wrap(err))
    end
    return xpcall(cb, error_handler, ...)
end

-- Call all callbacks from a list
function Future:_call_all(callbacks, message, ...)
    local all_ok = true
    local all_err = ''

    for _, cb in ipairs(callbacks) do
        local ok, err = self:_call(cb, message, ...)
        if not ok then
            all_ok = false
            all_err = all_err .. err
        end
    end

    return all_ok, all_err
end

-- Create an already-completed, successful future
local function make(...)
    return Future(_SUCCESS, ...)
end
M.make = make

-- Create an already-completed, failed future
local function make_error(v)
    return Future(_ERROR, v)
end
M.make_error = make_error

local function make_func(f, ...)
    local results = util.pack_n(xpcall(f, eh.wrap, ...))
    if not results[1] then
        return make_error(results[2])
    end
    return make(util.unpack_n(results, 2))
end
M.make_func = make_func

local Promise = pl.class()
M.Promise = Promise

-- Create an unfulfilled Promise. The Promise has an associated Future
-- (accessible with the future() method) which you can hand over to
-- consumers. You may fulfill the promise with set_value() or set_error().
function Promise:_init()
    self._future = Future(_PENDING)
end

-- Return the associated Future
function Promise:future()
    return self._future
end

-- Fulfill the promise with a successful completion
function Promise:set_value(...)
    self._future:_complete(_SUCCESS, ...)
end

-- Fulfill the promise with a failure completion
function Promise:set_error(v)
    self._future:_complete(_ERROR, v)
end

-- Fulfill the promise in the same way as the given future, which
-- must have completed
function Promise:set_from_future(f)
    assert(Future:class_of(f))
    assert(not f:is_pending())
    self._future:_complete(f._mode, util.unpack_n(f._result))
end

-- Helper to wrap a callback so that, if it throws, it sets the error on a
-- Promise.
local function wrap_error(promise, cb)
    return function(...)
        local ok, err = xpcall(cb, eh.wrap, ...)
        if not ok then
            promise:set_error(err)
        end
    end
end

-- Helper to propagate the error from a Future to a Promise; use as an
-- on_error callback on a Future.
local function propagate_error(promise)
    return function(e)
        promise:set_error(e)
    end
end

-- Forward all errors from a future to a promise
local function set_success_callback_and_forward_errors(f, p, callback)
    f:on_success(wrap_error(p, callback)):on_error(propagate_error(p))
end

-- map combinator
local function map(future, fn)
    local p = Promise()
    -- If future succeeds, pass it through fn
    -- If future fails, propagate the error
    local function callback(...)
        p:set_value(fn(...))
    end

    set_success_callback_and_forward_errors(future, p, callback)
    return p:future()
end
M.map = map
Future.map = map  -- for convenience

-- flat_map combinator
local function flat_map(future, fn)
    local p = Promise()
    local function callback(...)
        fn(...):on_completion(function(h) p:set_from_future(h) end)
    end

    set_success_callback_and_forward_errors(future, p, callback)
    return p:future()
end
M.flat_map = flat_map
Future.flat_map = flat_map   -- for convenience

-- enforce combinator
local function enforce(future, fn)
    local p = Promise()
    local function callback(...)
        if fn(...) then
            p:set_value(...)
        else
            p:set_error('Filter predicate failed')
        end
    end

    set_success_callback_and_forward_errors(future, p, callback)
    return p:future()
end
M.enforce = enforce
Future.enforce = enforce  -- for convenience

-- and_then combinator
local function and_then(future, fn)
    local p = Promise()
    -- If future succeeds, call fn()
    -- If future fails, propagate the error
    local function callback(...)
        fn(...)
        p:set_value(...)
    end

    set_success_callback_and_forward_errors(future, p, callback)
    return p:future()
end
M.and_then = and_then
Future.and_then = and_then  -- for convenience

-- reduce combinator
local function reduce_index(func, zero, ...)
    local n = select('#', ...)
    if n == 0 then
        return make({})
    end

    local results = zero

    local p = Promise()
    local errored = false
    for i = 1, n do
        local f = select(i, ...)

        local function success_callback(...)
            if not errored then
                local ok
                ok, results = xpcall(func, eh.wrap, results, i, ...)
                if not ok then
                    errored = true
                    p:set_error(results)
                else
                    n = n - 1
                    if n == 0 then
                        p:set_value(results)
                    end
                end
            end
        end

        local function error_callback(e)
            if not errored then
                errored = true
                p:set_error(e)
            end
        end

        f:on_success(success_callback):on_error(error_callback)
    end
    return p:future()
end
M.reduce_index = reduce_index


-- reduce combinator
local function reduce(func, zero, ...)
    local function strip_index(result, index, ...)
        return func(result, ...)
    end
    return reduce_index(strip_index, zero, ...)
end
M.reduce = reduce


-- all combinator
local function all(...)
    local function collect_first(result, i, v)
        result[i] = v
        return result
    end
    return reduce_index(collect_first, { n = select('#', ...) }, ...):
        map(util.unpack_n)
end
M.all = all

local function all_multi(...)
    local function collect_all(result, i, ...)
        result[i] = util.pack_n(...)
        return result
    end
    return reduce_index(collect_all, {}, ...):map(table.unpack)
end
M.all_multi = all_multi

-- any_index combinator
local function any_index(...)
    local n = select('#', ...)
    if n == 0 then
        return make_error('any() called with 0 futures')
    end

    local p = Promise()
    local done = false
    for i = 1, n do
        local f = select(i, ...)

        local function success_callback(...)
            if not done then
                done = true
                p:set_value(i, ...)
            end
        end

        local function error_callback(e)
            if not done then
                done = true
                p:set_error(e)
            end
        end

        f:on_success(success_callback):on_error(error_callback)
    end
    return p:future()
end
M.any_index = any_index


-- any combinator
local function any(...)
    local function strip_first(first, ...)
        return ...
    end
    return any_index(...):map(strip_first)
end
M.any = any

local function is_future(f)
    return Future:class_of(f)
end
M.is_future = is_future

return M
