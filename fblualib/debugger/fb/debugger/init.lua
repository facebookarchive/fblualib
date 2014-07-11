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
local breakpoint = require('fb.debugger.breakpoint')
local utils = require('fb.debugger.utils')
local types = require('fb.debugger.types')
local editline = require('fb.editline')
local completer = require('fb.editline.completer')

local ok, trepl = pcall(require, 'fb.trepl')
if not ok then
    trepl = nil
end

local current_hook_mode = ''
local stepping = false
local stepping_thread
local stepping_depth
local current_file_contents
local current_start_line
local current_line
local skip_levels = 0

local function prompt_exit()
    io.stdout:write('Do you really want to exit debugger ([y]/n)? ')
    io.flush()
    local line = io.read('*l')
    return line == '' or line:lower() == 'y'
end

-- Get hook mode appropriate for current debugger state
local function get_hook_mode()
    local mode = ''
    if stepping or breakpoint.has_file_breakpoints() then
        mode = mode .. 'l'
    end
    if breakpoint.has_function_breakpoints() then
        mode = mode .. 'c'
    end
    return mode
end

local debug_hook

-- Change hook mode if necessary
local function update_hook_mode(done)
    local new_hook_mode
    if done then
        new_hook_mode = ''
    else
        new_hook_mode = get_hook_mode()
    end
    if new_hook_mode ~= current_hook_mode then
        if new_hook_mode == '' then
            debug.sethook()
        else
            debug.sethook(debug_hook, new_hook_mode)
        end
        current_hook_mode = new_hook_mode
    end
end

-- A debug stack is a table of the following form:
-- { frames = {
--     { vars = <current vars for this frame>,
--       info = <frame info, as per getinfo('flLnS')>,
--     },
--   current_frame = <current_frame>,
--   thread = <current_coroutine>,
-- }

-- Return number of stack frames (excluding get_num_frames() frame)
local function get_num_frames()
    local n = 0
    while debug.getinfo(n + 2, 'n') do
        n = n + 1
    end
    return n
end

-- Return a table that saves all locals and upvalues so we can run user code
-- in this environment.
-- Inspired by http://luaforge.net/projects/clidebugger/
local function save_vars(level)
    level = level + 1  -- account for save_vars()
    local vars = {}

    local info = debug.getinfo(level, 'fn')
    local func = info.func

    local names = {}

    -- assign a name, avoiding conflicts
    local function assign_name(name, idx, prefix)
        -- If not an internal variable (starting with '('), try to use its
        -- real name
        if name then
            if name and string.sub(name, 1, 1) ~= '(' then
                if not names[name] then
                    return name
                end
            else
                prefix = prefix .. '_tmp'
            end
        end
        local nbase = string.format('_dbg%s_%d', prefix, idx)
        name = nbase
        local i = 1
        while names[name] do
            name = string.format('%s_%d', nbase, i)
            i = i + 1
        end
        return name
    end

    local locals = {}

    -- count locals, we'll process them in reverse order
    local nlocals = 0
    while debug.getlocal(level, nlocals + 1) do
        nlocals = nlocals + 1
    end

    for i = nlocals, 1, -1 do
        local name, value = debug.getlocal(level, i)
        assert(name)
        local new_name = assign_name(name, i, 'l')
        vars[new_name] = value
        locals[i] = {name, new_name}
        names[new_name] = true
    end
    vars['__LOCALS__'] = locals

    -- get varargs
    i = -1
    local p = nlocals + 1
    while true do
        local name, value = debug.getlocal(level, i)
        if not name then
            break
        end
        local new_name = assign_name(nil, -i, 'v')
        vars[new_name] = value
        locals[p] = {'', new_name}
        names[new_name] = true
        i = i - 1
        p = p + 1
    end

    local upvalues = {}

    if func then
        local nups = 0
        while debug.getupvalue(func, nups + 1) do
            nups = nups + 1
        end
        for i = nups, 1, -1 do
            local name, value = debug.getupvalue(func, i)
            assert(name)
            local new_name = assign_name(name, i, 'u')
            vars[new_name] = value
            upvalues[i] = {name, new_name}
            names[new_name] = true
        end
    end
    vars['__UPVALUES__'] = upvalues

    -- By default, all else are locals
    setmetatable(vars, {__index = getfenv(func), __newindex = getfenv(func)})
    return vars
end

local function restore_vars(level, vars)
    level = level + 1  -- account for restore_vars()
    local info = debug.getinfo(level, 'nf')
    local func = info.func

    if func then
        local upvalues = vars['__UPVALUES__']
        for i, names in pairs(vars['__UPVALUES__']) do
            debug.setupvalue(func, i, vars[names[2]])
        end
    end

    for i, names in pairs(vars['__LOCALS__']) do
        debug.setlocal(level, i, vars[names[2]])
    end
end

local DebugStack = pl.class()

local frame_offset = 4
local frame_skip_funcs

function DebugStack:_init()
    self.thread = coroutine.running()
    local n = get_num_frames() - frame_offset
    assert(n >= 0)

    self.current_frame = 1

    self.frames = {}
    local maybe_skip = true
    self.skip_frames = 0
    for i = 1, n do
        local fn = i + frame_offset
        local frame = {}
        frame.info = debug.getinfo(fn, 'flLnSu')
        frame.vars = save_vars(fn)
        self.frames[i] = frame
        if i == self.skip_frames + 1 and frame_skip_funcs[frame.info.func] then
            self.skip_frames = self.skip_frames + 1
        end
    end
end

function DebugStack:_error(fmt, ...)
    error(string.format(fmt, ...))
end

function DebugStack:help()
    print([==[Available commands:

help                This help (alias: h)

where               Show current stack (alias: backtrace, bt)
frame <n>           Set current frame to <n> (alias: f)
up                  Move one frame up
down                Move one frame down

break <location>    Set breakpoint at <location> (alias: b)
info breakpoints    Show breakpoints (alias: info b)
delete <id>         Delete breakpoint <id>
disable <id>        Disable breakpoint <id>
enable <id>         Enable breakpoint <id>

next                Execute one line (skip over function call) (alias: n)
step                Execute one line (step into function call) (alias: s)

locals              Print local variables for current stack frame
                    (alias: loc)
vlocals             Print local variables for current stack frame (with values)
                    (alias: vloc)
upvalues            Print upvalues for current stack frame
                    (alias: upv)
vupvalues           Print upvalues for current stack frame (with values)
                    (alias: vupv)
globals             Print globals (alias: glob)
vglobals            Print globals (with values) (alias: vglob)

exec <code>         Execute <code> in context of current stack frame (alias: e)
print <expr>        Evaluate <expr> in context of current stack frame
                    (alias: p)

list <location>     List code around <location> (alias: l)

quit                Quit debugger (alias: q)]==])
end
DebugStack.h = DebugStack.help

function DebugStack:frame(f)
    f = tonumber(f)
    if f < 1 or f > #self.frames - self.skip_frames then
        self:_error('Invalid frame ', f)
    end
    if f ~= self.current_frame then
        self.current_frame = f
        self:_new_location()
    end
end
DebugStack.f = DebugStack.frame
DebugStack.fr = DebugStack.frame

function DebugStack:up()
    self:frame(self.current_frame + 1)
end

function DebugStack:down()
    self:frame(self.current_frame - 1)
end

function DebugStack:_new_location()
    current_file_contents = nil
    current_start_line = nil
    current_line = nil
    self:_show_current()
end

-- execute code in user context
function DebugStack:_exec(code)
    local prev_env = getfenv(0)
    local vars = self.frames[self.current_frame + self.skip_frames].vars
    setfenv(0, vars)
    local ok, r = pcall(function()
        local func
        if type(code) == 'function' then
            func = code
            setfenv(func, vars)
        else
            func = loadstring(code)
            if not func then
                self:_error('Invalid Lua code: %s', code)
            end
        end
        return func()
    end)
    setfenv(0, prev_env)
    if ok then
        local frame_offset = get_num_frames() - #self.frames
        restore_vars(self.current_frame + self.skip_frames + frame_offset, vars)
        return r
    else
        print(r)
    end
end

if trepl then
    function DebugStack:repl()
        self:_exec(trepl.make_repl('D'))
    end
end

function DebugStack:exec(code)
    self:_exec(code)
end
DebugStack.e = DebugStack.exec

function DebugStack:print(expr)
    self:_exec('print(' .. expr .. ')')
end
DebugStack.p = DebugStack.print

function DebugStack:_eval(expr)
    return self:_exec('return (' .. expr .. ')')
end

-- Parse a location, either Lua function name or file:line
function DebugStack:_get_location(str)
    local info = self.frames[self.current_frame + self.skip_frames].info
    if not str or str == '' then
        if info.func and info.currentline and info.currentline > 0 then
            return info.func, info.currentline
        elseif (info.source and string.sub(info.source, 1, 1) == '@' and
                info.currentline and info.currentline > 0) then
            return info.source, info.currentline
        else
            error('Unable to accurately determine current location')
        end
    end
    local n = tonumber(str)
    if n then
        -- line in current file / expr
        if info.source and string.sub(info.source, 1, 1) == '@' then
            return info.source, n
        else
            error('Unable to accurately determine current file')
        end
    end
    local file, line = string.match(str, '^([%w_%-/%.]+):(%d+)$')
    if file then
        return '@' .. file, line
    end
    -- evaluate, maybe it's a function
    local success, val = pcall(function() return self:_eval(str) end)
    if success then
        local ty = type(val)
        if ty == 'function' then
            return val
        end
        self:_error("'%s' does not evaluate to function: %s",
                    str, ty)
    else
        self:_error("'%s' evaluation error: %s",
                    str, val)
    end
end

function DebugStack:list(str)
    local location, line
    if (not current_file_contents) or (str and str ~= '') then
        local location, line = self:_get_location(str)
        current_line = line
        current_start_line = nil

        if type(location) == 'function' then
            local info = debug.getinfo(location, 'S')
            if not info.source then
                error('Cannot find function definition!')
            end
            location = info.source
            current_line = line or info.linedefined
        end

        local kind = string.sub(location, 1, 1)
        if kind == '@' then
            current_file_contents = io.open(string.sub(location, 2)):read('*a')
        elseif kind == '=' then
            error("Cannot read source '%s'", location)
        else
            current_file_contents = location
        end
    end

    current_start_line = utils.print_numbered(
        current_file_contents,
        current_start_line,
        21,  -- lines of context
        current_line)
end
DebugStack.l = DebugStack.list

function DebugStack:_break(target)
    local location, line = self:_get_location(target)
    local bp
    if type(location) == 'function' then
        bp = breakpoint.FunctionBreakpoint(location, line)
    else
        bp = breakpoint.FileBreakpoint(location, line)
    end
    bp:register()
end
DebugStack['break'] = DebugStack._break
DebugStack.b = DebugStack._break
DebugStack.br = DebugStack._break

function DebugStack:info(str)
    if not str then
        self:_error('Usage: info <command>')
    end
    local fn = DebugStack['_info_' .. str]
    if not fn then
        self:_error("Unknown 'info' subcommand: '%s'", str)
    end
    fn(self)
end

function DebugStack:_info_breakpoints()
    breakpoint.list_breakpoints()
end
DebugStack._info_b = DebugStack._info_breakpoints
DebugStack._info_br = DebugStack._info_breakpoints

function DebugStack:enable(id)
    id = tonumber(id)
    breakpoint.get(id).enabled = true
end

function DebugStack:disable(id)
    id = tonumber(id)
    breakpoint.get(id).enabled = false
end

function DebugStack:delete(id)
    id = tonumber(id)
    breakpoint.get(id):unregister()
end

local type_names = {
    main = 'main chunk',
    tail = 'tail call',
    C = 'C function',
    Lua = 'function',
    metamethod = 'metamethod',
}

function DebugStack:_frame_to_str(i, show_current_marker)
    local frame = self.frames[i + self.skip_frames]
    local frame_msg = ' '
    if show_current_marker and i == self.current_frame then
        frame_msg = '>'
    end

    local msg = string.format('#%-2d %s ', i, frame_msg)

    local info = frame.info

    msg = msg .. (type_names[info.what] or info.what)
    if info.name then
        msg = msg .. ' ' .. info.name
        local nparams = info.nparams
        if nparams then
            msg = msg .. '('
            local locals = frame.vars['__LOCALS__']
            for i = 1, nparams do
                if i ~= 1 then
                    msg = msg .. ', '
                end
                msg = msg .. locals[i][1]
            end
            if info.isvararg then
                if nparams ~= 0 then
                    msg = msg .. ', '
                end
                msg = msg .. '...'
            end
            msg = msg .. ')'
        end
    end

    if info.source then
        msg = msg .. ' ' .. info.source
    end

    if info.currentline and info.currentline >= 0 then
        msg = msg .. ':' .. info.currentline
    end
    return msg
end

function DebugStack:backtrace(expr)
    for i = 1, #self.frames - self.skip_frames do
        print(self:_frame_to_str(i, true))
    end
end
DebugStack.bt = DebugStack.backtrace
DebugStack.where = DebugStack.backtrace

function DebugStack:_show_current()
    print(self:_frame_to_str(self.current_frame))
end

function DebugStack:cont()
    return 'continue'
end
DebugStack['continue'] = DebugStack.cont
DebugStack.c = DebugStack.cont

function DebugStack:quit()
    if prompt_exit() then
        return 'quit'
    end
end
DebugStack.q = DebugStack.quit

function DebugStack:locals()
    self:_dump('__LOCALS__', false, true)
end
DebugStack.loc = DebugStack.locals

function DebugStack:upvalues()
    self:_dump('__UPVALUES__', false)
end
DebugStack.upv = DebugStack.upvalues

function DebugStack:vlocals()
    self:_dump('__LOCALS__', true, true)
end
DebugStack.vloc = DebugStack.vlocals

function DebugStack:vupvalues()
    self:_dump('__UPVALUES__', true)
end
DebugStack.vupv = DebugStack.vupvalues

function DebugStack:globals()
    self:_dump_globals(false)
end
DebugStack.glob = DebugStack.globals

function DebugStack:vglobals()
    self:_dump_globals(true)
end
DebugStack.vglob = DebugStack.vglobals

function DebugStack:_dump(key, print_value, printing_locals)
    local frame = self.frames[self.current_frame + self.skip_frames]
    local info = frame.info
    local vars = frame.vars
    local var_names = vars[key]
    local fmt = '%-7s %-20s %-20s %-20s %s\n'
    local value_header
    if print_value then
        value_header = 'Value'
    else
        value_header = 'Summary'
    end
    printf(fmt, 'P/L/U', 'Name', 'Original name', 'Type', value_header)
    for i, names in ipairs(var_names) do
        local name = names[2]
        local orig_name = names[1]
        if orig_name == name then
            orig_name = ''
        end
        local value = vars[name]
        local vtype = types.type(value)
        if not print_value then
            value = types.pretty_print(value, 40)
        end
        local t
        if printing_locals then
            if pl.stringx.startswith(name, '_dbgv') then
                t = 'Vararg'
            elseif info.nparams then
                if i <= info.nparams then
                    t = 'Param'
                else
                    t = 'Local'
                end
            else
                t = '?'
            end
        else
            t = 'Upval'
        end
        printf(fmt, t, name, orig_name, vtype, value)
    end
end

function DebugStack:_dump_globals(print_value)
    local fmt = '%-20s %-20s %s\n'
    local value_header
    if print_value then
        value_header = 'Value'
    else
        value_header = 'Summary'
    end
    printf(fmt, 'Name', 'Type', value_header)
    for name, value in pl.tablex.sort(_G) do
        local vtype = types.type(value)
        if not print_value then
            value = types.pretty_print(value, 40)
        end
        printf(fmt, name, vtype, value)
    end
end

function DebugStack:_get_locals_and_upvalues()
    local ret = {}
    self:_add_names(ret, '__LOCALS__')
    self:_add_names(ret, '__UPVALUES__')
    return ret
end

function DebugStack:_add_names(out, key)
    local vars = self.frames[self.current_frame + self.skip_frames].vars
    local var_names = vars[key]
    for _, names in pairs(var_names) do
        local name = names[2]
        out[name] = vars[name]
    end
end

function DebugStack:gs()
    for k, _ in pairs(self:_get_locals_and_upvalues()) do
        print(k)
    end
end

function DebugStack:step()
    stepping = true
    stepping_thread = self.thread
    stepping_depth = nil
    return 'continue'
end
DebugStack.s = DebugStack.step

function DebugStack:next()
    stepping = true
    stepping_thread = self.thread
    stepping_depth = #self.frames - self.current_frame - self.skip_frames + 1
    return 'continue'
end
DebugStack.n = DebugStack.next

function DebugStack:_process(line)
    local p, q = string.find(line, '%w[%w_]*')
    if not p then
        return
    end
    local cmd = string.lower(string.sub(line, p, q))
    local args
    p = string.find(line, '[^%s]', q + 1)
    if p then
        args = string.sub(line, p)
    end
    local fn = self[cmd]
    if not fn then
        self:_error("Invalid command '%s'", cmd)
    end
    return fn(self, args)
end

local function read_line()
    io.stdout:write('DEBUG> ')
    return io.stdin:read()
end

local function debug_io_repl(dstack)
    dstack:_new_location()
    while true do
        local line = read_line()
        local mode
        if line then
            local status, m = pcall(
                function() return dstack:_process(line) end)
            if not status then
                print('Error: ' .. m)
            else
                mode = m
            end
        end
        if mode == 'continue' then
            return true
        elseif mode == 'quit' then
            return false
        end
    end
end

local eline_dstack
local eline_keywords = {}
local skip_keywords = pl.tablex.makeset({'is_a', 'class_of', 'cast', 'catch'})
for k, v in pairs(DebugStack) do
    -- not stuff inherited from pl.class
    if k:sub(1, 1) ~= '_' and not skip_keywords[k] then
        table.insert(eline_keywords, k)
    end
end

local eline = editline.EditLine({
    prompt = 'DEBUG> ',
    complete = function(word, line, startpos, endpos)
        return completer.complete(
            word, line, startpos, endpos,
            eline_keywords,
            function() return eline_dstack:_get_locals_and_upvalues() end)
    end,
    history_file = os.getenv('HOME') .. '/.lua_debug_history',
    auto_history = true,
})

local function debug_readline_repl(dstack)
    dstack:_new_location()
    eline_dstack = dstack
    while true do
        local line = eline:read()
        if not line then
            io.write('\n')
            if prompt_exit() then
                return false
            end
        else
            line = pl.stringx.strip(line)
            local status, m = pcall(function() return dstack:_process(line) end)
            if not status then
                print('Error: ' .. m)
            elseif m == 'continue' then
                return true
            elseif m == 'quit' then
                return false
            end
        end
    end
end

local debug_repl = debug_readline_repl

local stack_level = {}

function debug_hook(event, line_num)
    local info
    local has_source = false
    if event == 'line' then
        local do_break = false
        if stepping and stepping_thread == coroutine.running() then
            if (not stepping_depth) or
               ((get_num_frames() - 1) <= stepping_depth) then
               stepping = false
               stepping_thread = nil
               stepping_depth = nil
               do_break = true
            end
        end
        if breakpoint.has_breakpoints_at_line(line_num) then
            local function check_file_breakpoints()
                info = debug.getinfo(3, 'nlS')
                local filename = info.source
                if not filename or string.sub(filename, 1, 1) ~= '@' then
                    return
                end
                filename = string.sub(filename, 2)
                local p = 0
                local bps
                while p and not bps do
                    filename = string.sub(filename, p + 1)
                    p = string.find(filename, '/', 1, true)
                    bps = breakpoint.get_file_breakpoints(filename, line_num)
                end
                if not bps then
                    return
                end
                local nhit = 0
                for _,b in ipairs(bps) do
                    if b:matches(info) then
                        do_break = true
                        b:hit()
                        nhit = nhit + 1
                    end
                end
            end
            check_file_breakpoints()
        end
        if not do_break then
            return
        end
    elseif event == 'call' then
        if not breakpoint.has_function_breakpoints() then
            return
        end
        info = debug.getinfo(2, 'nf')
        local bps = breakpoint.get_function_breakpoints(info.func)
        if not bps then
            return
        end
        local nhit = 0
        local do_break = false
        for _,b in ipairs(bps) do
            if b:matches(info) then
                do_break = true
                b:hit()
                nhit = nhit + 1
            end
        end
        if not do_break then
            return
        end
    else
        return  -- ignore unsupported events
    end
    local done = false
    if not debug_repl(DebugStack()) then
        print('Debugger exited')
        done = true
    end
    update_hook_mode(done)
end

local function do_breakpoint(target)
    local bp
    if type(target) == 'function' then
        bp = breakpoint.FunctionBreakpoint(target)
    else
        local p = string.find(target, ':', 1, true)
        if not p then
            error('File breakpoint must be of the form file:line_number')
        end
        local file_name = string.sub(target, 1, p - 1)
        local line_number = tonumber(string.sub(target, p + 1))
        bp = breakpoint.FileBreakpoint('@' .. file_name, line_number)
    end
    return bp:register()
end

local M = { }

function M.set_breakpoint(target)
    local id = do_breakpoint(target)
    update_hook_mode()
    return id
end

function M.delete_breakpoint(id)
    breakpoint.get(id):unregister()
    update_hook_mode()
end

local function enter_hook() end

local function enter()
    local bp = breakpoint.FunctionBreakpoint(enter_hook)
    bp.quiet = true
    bp.hit_action = function() bp:unregister() end
    bp:register()
    update_hook_mode()
    enter_hook()
end
M.enter = enter

frame_skip_funcs = pl.tablex.makeset({enter_hook, enter, error})

local function add_skip_func(func)
    frame_skip_funcs[func] = true
end
M.add_skip_func = add_skip_func

return M
