--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

local pl = require('pl.import_into')()
local ffi = require('ffi')
local util = require('fb.util')
local module_config = require('fb.editline._config')

local M = {}

ffi.cdef([==[
struct editline;
typedef struct editline EditLine;

/* el_init(const char*, FILE*, FILE*, FILE*); */
EditLine* el_init(const char*, void*, void*, void*);
void el_end(EditLine*);
void el_reset(EditLine*);

const char* el_gets(EditLine*, int*);
int el_getc(EditLine*, char*);
void el_push(EditLine*, const char*);

void el_beep(EditLine*);

typedef char* (*el_pfunct_t)(EditLine*);
int el_set(EditLine*, int, ...);
int el_get(EditLine*, int, ...);

typedef unsigned char (*el_addfn)(EditLine*, int);
unsigned char _el_fn_complete(EditLine*, int);
int el_source(EditLine*, const char*);

void el_resize(EditLine*);

typedef struct lineinfo {
    const char* buffer;
    const char* cursor;
    const char* lastchar;
} LineInfo;

const LineInfo* el_line(EditLine*);
int el_insertstr(EditLine*, const char*);
void el_deletestr(EditLine*, int);

struct history;
typedef struct history History;

typedef struct HistEvent {
    int num;
    const char* str;
} HistEvent;

History* history_init(void);
void history_end(History*);

int history(History*, HistEvent*, int, ...);

struct tokenizer;
typedef struct tokenizer Tokenizer;

Tokenizer* tok_init(const char*);
void tok_end(Tokenizer*);
void tok_reset(Tokenizer*);
int tok_line(Tokenizer*, const LineInfo*, int*, const char***, int*, int*);
int tok_str(Tokenizer*, const char*, int*, const char***);

/* Ayiee. */
int fn_complete(
    EditLine* el,
    char* (*complet_func)(const char*, int),
    char** (*attempted_completion_function)(const char*, int, int),
    const char* word_break,
    const char* special_prefixes,
    const char* (*app_func)(const char*),
    size_t query_items,
    int* completion_type,
    int* over,
    int* point,
    int* end);

void* stdin;
void* stdout;
void* stderr;

char* strdup(const char*);
void* malloc(size_t);
void free(void*);
int isatty(int);
]==])

local lib = ffi.load(module_config.libedit)

-- I know regular expressions.
local EL_PROMPT      =  0
local EL_TERMINAL    =  1
local EL_EDITOR      =  2
local EL_SIGNAL      =  3
local EL_BIND        =  4
local EL_TELLTC      =  5
local EL_SETTC       =  6
local EL_ECHOTC      =  7
local EL_SETTY       =  8
local EL_ADDFN       =  9
local EL_HIST        =  10
local EL_EDITMODE    =  11
local EL_RPROMPT     =  12
local EL_GETCFN      =  13
local EL_CLIENTDATA  =  14
local EL_UNBUFFERED  =  15
local EL_PREP_TERM   =  16
local EL_GETTC       =  17
local EL_GETFP       =  18
local EL_SETFP       =  19
local EL_REFRESH     =  20
local EL_PROMPT_ESC  =  21
local EL_RPROMPT_ESC =  22
local EL_RESIZE      =  23

local H_FUNC         =  0
local H_SETSIZE      =  1
local H_GETSIZE      =  2
local H_FIRST        =  3
local H_LAST         =  4
local H_PREV         =  5
local H_NEXT         =  6
local H_CURR         =  8
local H_SET          =  7
local H_ADD          =  9
local H_ENTER        =  10
local H_APPEND       =  11
local H_END          =  12
local H_NEXT_STR     =  13
local H_PREV_STR     =  14
local H_NEXT_EVENT   =  15
local H_PREV_EVENT   =  16
local H_LOAD         =  17
local H_SAVE         =  18
local H_CLEAR        =  19
local H_SETUNIQUE    =  20
local H_GETUNIQUE    =  21
local H_DEL          =  22
local H_NEXT_EVDATA  =  23
local H_DELDATA      =  24
local H_REPLACE      =  25

local CC_NORM         = 0
local CC_NEWLINE      = 1
local CC_EOF          = 2
local CC_ARGHACK      = 3
local CC_REFRESH      = 4
local CC_CURSOR       = 5
local CC_ERROR        = 6
local CC_FATAL        = 7
local CC_REDISPLAY    = 8
local CC_REFRESH_BEEP = 9

local EditLine = pl.class()

local c_int = ffi.typeof('int')

local default_word_break = " \t\n\"\\'><=;:+-*/%^~#{}()[].,"
local default_max_matches = 100

local editrc_file = os.getenv('HOME') .. '/.editrc'

local mutex = util.get_mutex('fb.editline.mutex')

local function create_editrc()  -- ignoring race conditions...
    local f = io.open(editrc_file, 'r')
    if f then  -- already exists
        f:close()
        return
    end

    f = io.open(editrc_file, 'w')
    if not f then
        -- Couldn't create editrc, no big deal
        return
    end
    f:write([==[
# disk space is cheap...
history size 1000000
history unique 1

# emacs bindings by default
bind -e

bind ^I complete
bind ^R em-inc-search-prev
]==])
    f:close()
end

-- config:
-- {
--   prompt = <prompt to display>,
--   prompt_func = <function that returns prompt>,
--   history_file = <path to history file>,
--   word_break = <string of word break characters>,
--   complete = <completion function>,
--   max_matches = <max matches before prompt 'Display all N possibilities'>,
--   auto_history = <bool: automatically add lines to history?>
-- }
--
-- completion function: function(word, line, start_idx, end_idx)
-- 'word' is the last word (according to word break characters)
-- 'line' is the whole line;
-- line:sub(start_idx, end_idx) == word
-- returns list of matches, final_str
-- final_str will be automatically appended if we have only one match
--   (used to close strings and the like)
function EditLine:_init(config)
    self.config = config

    create_editrc()

    local function prompt_cb(el)
        local prompt
        if config.prompt_func then
            prompt = config.prompt_func()
        elseif config.prompt then
            prompt = config.prompt
        else
            prompt = '> '
        end
        self.prompt_buf = ffi.gc(ffi.C.strdup(prompt), ffi.C.free)
        return self.prompt_buf
    end
    prompt_cb = ffi.new('el_pfunct_t', prompt_cb)

    -- Initialize editline
    self.editline = util.call_locked(
        mutex,
        lib.el_init,
        'trepl', ffi.C.stdin, ffi.C.stdout, ffi.C.stderr)
    ffi.gc(self.editline,
           function() util.call_locked(mutex, lib.el_end) end)

    -- Set completion function
    local function complete_func(el, ch)
        if not config.complete then
            return CC_NORM  -- do nothing
        end

        local line_info = lib.el_line(el)
        local buffer = ffi.string(line_info.buffer,
                                  line_info.lastchar - line_info.buffer)

        local over = ffi.new('int[1]')
        local final_str
        local function complete_cb(word, start_idx, end_idx)
            word = ffi.string(word)

            local matches
            matches, final_str = config.complete(
                word, buffer, start_idx + 1, end_idx)
            final_str = final_str or ''

            if not matches then
                return nil
            end

            over[0] = 1  -- don't call default completer

            if not matches[1] then
                return nil
            end

            -- matches[0] is lowest common prefix
            -- matches[N+1] is NULL to indicate end
            local array_matches = ffi.cast('char**', ffi.C.malloc(
                ffi.sizeof('char*') * (#matches + 2)))

            array_matches[0] = ffi.C.strdup(
                util.longest_common_prefix(matches))
            for i,s in ipairs(matches) do
                array_matches[i] = ffi.C.strdup(s)
            end
            array_matches[#matches + 1] = nil

            return array_matches
        end

        local function append_final_char_cb(s)
            return final_str
        end

        return lib.fn_complete(
            el,
            nil,
            complete_cb,
            config.word_break or default_word_break,
            nil,
            append_final_char_cb,
            config.max_matches or default_max_matches,
            nil,
            over,
            nil,
            nil)
    end
    complete_func = ffi.cast('el_addfn', complete_func)

    util.call_locked(mutex, lib.el_set, self.editline, EL_ADDFN,
                     "complete", "complete", complete_func)

    -- Initialize history; save history on destruction
    self.history = util.call_locked(mutex, lib.history_init)
    self.hist_event = ffi.new('HistEvent')
    local function save_and_end_history()
        if config.history_file then
            util.call_locked(mutex, lib.history, self.history, self.hist_event,
                             H_SAVE, config.history_file)
        end
        util.call_locked(mutex, lib.history_end, self.history)
    end
    ffi.gc(self.history, save_and_end_history)

    -- Associate EditLine with history
    util.call_locked(mutex, lib.el_set, self.editline, EL_HIST, lib.history,
                     self.history)

    -- Source ~/.editrc
    util.call_locked(mutex, lib.el_source, self.editline, nil)

    util.call_locked(mutex, lib.el_set, self.editline, EL_PROMPT, prompt_cb)

    -- Load history file
    if config.history_file then
        util.call_locked(mutex, lib.history, self.history, self.hist_event,
                         H_LOAD, config.history_file)
    end
end

local c_string = ffi.typeof('char*')
local null_string = c_string()

function EditLine:read()
    local length = ffi.new('int[1]')
    local line = util.call_locked(mutex, lib.el_gets, self.editline, length)

    if line ~= null_string then
        line = ffi.string(line, length[0])

        if self.config.auto_history then
            self:add_history(line)
        end
        return line
    end
end

function EditLine:add_history(line)
    if not line then
        return
    end
    line = pl.stringx.rstrip(line, '\r\n')
    if line ~= '' then
        util.call_locked(mutex, lib.history, self.history, self.hist_event,
                         H_ENTER, line)
    end
end

local FakeEditLine = pl.class()

function FakeEditLine:_init(config)
    self.config = config
end

function FakeEditLine:read()
    -- output prompt
    local prompt
    if self.config.prompt_func then
        prompt = self.config.prompt_func()
    elseif self.config.prompt then
        prompt = self.config.prompt
    else
        prompt = '> '
    end

    io.stdout:write(prompt)
    io.stdout:flush()

    return io.stdin:read('*l')
end

function FakeEditLine:add_history()
end

-- Only do line editing if
local tty = (ffi.C.isatty(0) == 1) and (ffi.C.isatty(1) == 1)
M.tty = tty

if tty then
    M.EditLine = EditLine
else
    M.EditLine = FakeEditLine
end

return M
