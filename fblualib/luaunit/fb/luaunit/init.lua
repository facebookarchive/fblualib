--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--
-- Unit test framework. Based heavily on LuaUnit, see below.

--[[
luaunit.lua

Description: A unit testing framework
Homepage: https://github.com/bluebird75/luaunit
Initial author: Ryu, Gwang (http://www.gpgstudy.com/gpgiki/LuaUnit)
Lot of improvements by Philippe Fremy <phil@freehackers.org>
License: BSD License, see LICENSE.txt
]]--

local pl = require('pl.import_into')()
local DEFAULT_VERBOSITY = 1
local cjson = require('cjson')

function assertError(f, ...)
    -- assert that calling f with the arguments will raise an error
    -- example: assertError( f, 1, 2 ) => f(1,2) should generate an error
    local ok, error_msg = pcall(f, ... )
    if not ok then return end
    error( "Expected an error but no error generated", 2 )
end

function assertErrorMessage(expected_msg, f, ...)
    -- assert that calling f with the arguments will raise an error that
    -- matches the given message (a pattern)
    local ok, error_msg = pcall(f, ...)
    if not ok then
        if string.match(error_msg, expected_msg) then
            return
        end
        error(string.format(
            'Expected error message that matches [%s], but got [%s]',
            expected_msg, error_msg), 2)
    end
    error('Expected an error but no error generated', 2)
end

local function mytostring( v )
    if type(v) == 'string' then
        return '"' .. v .. '"'
    end
    if type(v) == 'table' then
        if v.__class__ then
            return string.gsub( tostring(v), 'table', v.__class__ )
        end
        return tostring(v)
    end
    return tostring(v)
end

local function errormsg(expected, actual)
    local errorMsg
    if type(expected) == 'string' then
        errorMsg = "\nexpected: " .. mytostring(expected) .. "\n"..
        "actual  : " .. mytostring(actual) .. "\n"
    else
        errorMsg = "expected: " ..  mytostring(expected) ..
        ", actual: " .. mytostring(actual)
    end
    return errorMsg
end

local function _is_table_equals(actual, expected)
    if (type(actual) == 'table') and (type(expected) == 'table') then
        local k,v
        for k,v in ipairs(actual) do
            if not _is_table_equals(v, expected[k]) then
                return false
            end
        end
        for k,v in pairs(actual) do
            if type(k) ~= 'number' and not _is_table_equals(v, expected[k]) then

                return false
            end
        end
        return true
    elseif type(actual) ~= type(expected) then
        return false
    elseif actual == expected then
        return true
    end
    return false
end

function assertEquals(actual, expected)
    if type(actual) == 'table' and type(expected) == 'table' then
        if not _is_table_equals(actual, expected) then
            error( errormsg(actual, expected), 2 )
        end
    elseif type(actual) ~= type(expected) then
        error( errormsg(actual, expected), 2 )
    elseif actual ~= expected then
        error( errormsg(actual, expected), 2 )
    end
end

function assertTrue(value)
    if not value then
        error("expected: true, actual: " .. mytostring(value), 2)
    end
end

function assertFalse(value)
    if value then
        error("expected: false, actual: " .. mytostring(value), 2)
    end
end

function assertNotEquals(actual, expected)
    if type(actual) == 'table' and type(expected) == 'table' then
        if _is_table_equals(actual, expected) then
            error( errormsg(actual, expected), 2 )
        end
    elseif type(actual) == type(expected) and actual == expected  then
        error( errormsg(actual, expected), 2 )
    end
end

function assertIsNumber(value)
    if type(value) ~= 'number' then
        error("expected: a number value, actual:" .. type(value))
    end
end

function assertIsString(value)
    if type(value) ~= "string" then
        error("expected: a string value, actual:" .. type(value))
    end
end

function assertIsTable(value)
    if type(value) ~= 'table' then
        error("expected: a table value, actual:" .. type(value))
    end
end

function assertIsBoolean(value)
    if type(value) ~= 'boolean' then
        error("expected: a boolean value, actual:" .. type(value))
    end
end

function assertIsNil(value)
    if value ~= nil then
        error("expected: a nil value, actual:" .. type(value))
    end
end

function assertIsFunction(value)
    if type(value) ~= 'function' then
        error("expected: a function value, actual:" .. type(value))
    end
end

function assertIs(actual, expected)
    if actual ~= expected then
        error( errormsg(actual, expected), 2 )
    end
end

function assertNotIs(actual, expected)
    if actual == expected then
        error( errormsg(actual, expected), 2 )
    end
end

assert_equals = assertEquals
assert_not_equals = assertNotEquals
assert_error = assertError
assert_true = assertTrue
assert_false = assertFalse
assert_is_number = assertIsNumber
assert_is_string = assertIsString
assert_is_table = assertIsTable
assert_is_boolean = assertIsBoolean
assert_is_nil = assertIsNil
assert_is_function = assertIsFunction
assert_is = assertIs
assert_not_is = assertNotIs

local function prefixString( prefix, s )
    local lines = pl.tablex.splitlines(s)
    return prefix .. table.concat(lines, '\n' .. prefix)
end

----------------------------------------------------------------
--                     class TapOutput
----------------------------------------------------------------

local TapOutput = pl.class()

function TapOutput:_init()
    self.verbosity = 0
end
function TapOutput:startSuite() end
function TapOutput:startClass(className) end
function TapOutput:startTest(testName) end

function TapOutput:addFailure( errorMsg, stackTrace )
    print(string.format(
        "not ok %d\t%s", self.result.testCount, self.result.currentTestName ))
    if self.verbosity > 0 then
        print( prefixString( '    ', errorMsg ) )
    end
    if self.verbosity > 1 then
        print( prefixString( '    ', stackTrace ) )
    end
end

function TapOutput:endTest(testHasFailure)
    if not self.result.currentTestHasFailure then
        print(string.format(
            "ok     %d\t%s",
            self.result.testCount, self.result.currentTestName ))
    end
end

function TapOutput:endClass() end

function TapOutput:endSuite()
    print("1.." .. self.result.testCount)
    return self.result.failureCount
end


-- class TapOutput end

----------------------------------------------------------------
--                     class JUnitOutput
----------------------------------------------------------------

local JUnitOutput = pl.class()

function JUnitOutput:_init()
    self.verbosity = 0
end
function JUnitOutput:startSuite() end
function JUnitOutput:startClass(className)
    xmlFile = io.open(string.lower(className) .. ".xml", "w")
    xmlFile:write('<testsuite name="' .. className .. '">\n')
end
function JUnitOutput:startTest(testName)
    if xmlFile then
        xmlFile:write(
            '<testcase classname="' .. self.result.currentClassName ..
            '" name="' .. testName .. '">')
    end
end

function JUnitOutput:addFailure( errorMsg, stackTrace )
    if xmlFile then
        xmlFile:write(
            '<failure type="lua runtime error">' .. errorMsg .. '</failure>\n')
        xmlFile:write(
            '<system-err><![CDATA[' .. stackTrace .. ']]></system-err>\n')
    end
end

function JUnitOutput:endTest(testHasFailure)
    if xmlFile then xmlFile:write('</testcase>\n') end
end

function JUnitOutput:endClass() end

function JUnitOutput:endSuite()
    if xmlFile then xmlFile:write('</testsuite>\n') end
    if xmlFile then xmlFile:close() end
    return self.result.failureCount
end


-- class TapOutput end

local FBJsonOutput = pl.class()

function FBJsonOutput:startSuite() end
function FBJsonOutput:startClass(className) end
function FBJsonOutput:startTest(testName)
    io.stderr:write(cjson.encode({
        op = 'start',
        test = self.result.currentTestName
    }))
    io.stderr:write('\n')
    self.failureMessages = {}
end

function FBJsonOutput:addFailure(errorMsg, stackTrace)
    table.insert(self.failureMessages, errorMsg)
end

function FBJsonOutput:endTest()
    local status = ''
    local details = ''
    if self.result.currentTestHasFailure then
        status = 'failed'
        details = table.concat(self.failureMessages, '\n')
    else
        status = 'passed'
    end
    io.stderr:write(cjson.encode({
        op = 'test_done',
        test = self.result.currentTestName,
        status = status,
        details = details
    }))
    io.stderr:write('\n')
    self.failureMessages = {}
end

function FBJsonOutput:endClass() end
function FBJsonOutput:endSuite()
    io.stderr:write(cjson.encode({
        op = 'all_done',
    }))
    io.stderr:write('\n')
end

----------------------------------------------------------------
--                     class TextOutput
----------------------------------------------------------------

local TextOutput = pl.class()
function TextOutput:_init()
    self.runner = nil
    self.result = nil
    self.errorList ={}
    self.verbosity = 1
end

function TextOutput:startSuite()
end

function TextOutput:startClass(className)
    if self.verbosity > 0 then
        print( '>>>>>>>>> ' .. self.result.currentClassName )
    end
end

function TextOutput:startTest(testName)
    if self.verbosity > 0 then
        print( ">>> " .. self.result.currentTestName )
    end
end

function TextOutput:addFailure( errorMsg, stackTrace )
    table.insert(
        self.errorList, { self.result.currentTestName, errorMsg, stackTrace } )
    if self.verbosity == 0 then
        io.stdout:write("F")
    end
    if self.verbosity > 0 then
        print( errorMsg )
        print( 'Failed' )
    end
end

function TextOutput:endTest(testHasFailure)
    if not testHasFailure then
        if self.verbosity > 0 then
            --print ("Ok" )
        else
            io.stdout:write(".")
        end
    end
end

function TextOutput:endClass()
    if self.verbosity > 0 then
        print()
    end
end

function TextOutput:displayOneFailedTest( failure )
    testName, errorMsg, stackTrace = unpack( failure )
    print(">>> " .. testName .. " failed")
    print( errorMsg )
    if self.verbosity > 1 then
        print( stackTrace )
    end
end

function TextOutput:displayFailedTests()
    if #self.errorList == 0 then return end
    print("Failed tests:")
    print("-------------")
    for i,v in ipairs(self.errorList) do
        self:displayOneFailedTest( v )
    end
    print()
end

function TextOutput:endSuite()
    if self.verbosity == 0 then
        print()
    else
        print("=========================================================")
    end
    self:displayFailedTests()
    local successPercent, successCount
    successCount = self.result.testCount - self.result.failureCount
    if self.result.testCount == 0 then
        successPercent = 100
    else
        successPercent = math.ceil( 100 * successCount / self.result.testCount )
    end
    print( string.format("Success : %d%% - %d / %d",
    successPercent, successCount, self.result.testCount) )
end


-- class TextOutput end


----------------------------------------------------------------
--                     class NilOutput
----------------------------------------------------------------

local NilOutput = pl.class()
local function nop() end
NilOutput:catch(function(self, name) return nop end)

----------------------------------------------------------------
--                     class LuaUnit
----------------------------------------------------------------

LuaUnit = pl.class()

function LuaUnit:_init()
    self.verbosity = DEFAULT_VERBOSITY
end

-----------------[[ Utility methods ]]---------------------

function LuaUnit.isFunction(aObject)
    return 'function' == type(aObject)
end

--------------[[ Output methods ]]-------------------------

function LuaUnit:ensureSuiteStarted( )
    if self.result and self.result.suiteStarted then
        return
    end
    self:startSuite()
end

function LuaUnit:startSuite()
    self.result = {}
    self.result.failureCount = 0
    self.result.testCount = 0
    self.result.currentTestName = ""
    self.result.currentClassName = ""
    self.result.currentTestHasFailure = false
    self.result.suiteStarted = true
    self.outputType = self.outputType or TextOutput
    self.output = self.outputType()
    self.output.runner = self
    self.output.result = self.result
    self.output.verbosity = self.verbosity
    self.output:startSuite()
end

function LuaUnit:startClass( className )
    self.result.currentClassName = className
    self.output:startClass( className )
end

function LuaUnit:startTest( testName  )
    self.result.currentTestName = testName
    self.result.testCount = self.result.testCount + 1
    self.result.currentTestHasFailure = false
    self.output:startTest( testName )
end

function LuaUnit:addFailure( errorMsg, stackTrace )
    if not self.result.currentTestHasFailure then
        self.result.failureCount = self.result.failureCount + 1
        self.result.currentTestHasFailure = true
    end
    self.output:addFailure( errorMsg, stackTrace )
end

function LuaUnit:endTest()
    self.output:endTest( self.result.currentTestHasFailure )
    self.result.currentTestName = ""
    self.result.currentTestHasFailure = false
end

function LuaUnit:endClass()
    self.output:endClass()
end

function LuaUnit:endSuite()
    self.result.suiteStarted = false
    self.output:endSuite()
end

local output_types = {
    NIL = NilOutput,
    TAP = TapOutput,
    JUNIT = JUnitOutput,
    TEXT = TextOutput,
    FB_JSON = FBJsonOutput,
}

function LuaUnit:setOutputType(outputType)
    self.outputType = output_types[string.upper(outputType)]
    if not self.outputType then
        error( 'No such format: ' .. outputType)
    end
end

function LuaUnit:setVerbosity( verbosity )
    self.verbosity = verbosity
end

--------------[[ Runner ]]-----------------

SPLITTER = '\n>----------<\n'

local abort_on_error = os.getenv('LUAUNIT_ABORT_ON_ERROR')

function LuaUnit:protectedCall( classInstance , methodInstance)
    if abort_on_error then
        methodInstance(classInstance)
        return true
    end

    -- if classInstance is nil, this is just a function run
    local function err_handler(e)
        return debug.traceback(e .. SPLITTER, 4)
    end

    local ok, errorMsg = xpcall(methodInstance, err_handler, classInstance)
    if not ok then
        t = pl.stringx.split(errorMsg, SPLITTER)
        local stackTrace = string.sub(t[2] or '',2)
        self:addFailure( t[1], stackTrace )
    end

    return ok
end

function LuaUnit:_runTestMethod(
    classPath, classInstance, methodName, methodInstance)
    -- When executing a class method, all parameters are set
    -- When executing a (global) test function, classPath is [] and
    -- classInstance is nil

    if type(methodInstance) ~= 'function' then
        error(
            tostring(methodName) .. 'must be a function, not ' ..
            type(methodInstance))
    end

    local className = table.concat(classPath, '.')
    if className == '' then
        className = self.TEST_FUNCTION_PREFIX
    end

    if self.lastClassName ~= className then
        if self.lastClassName ~= nil then
            self:endClass()
        end
        self:startClass( className )
        self.lastClassName = className
    end

    self:startTest(className .. ':' .. methodName)

    -- run setUp first(if any)
    if classInstance and self.isFunction( classInstance.setUp ) then
        self:protectedCall( classInstance, classInstance.setUp)
    end

    -- run testMethod()
    if not self.result.currentTestHasFailure then
        self:protectedCall( classInstance, methodInstance)
    end

    -- lastly, run tearDown(if any)
    if classInstance and self.isFunction(classInstance.tearDown) then
        self:protectedCall( classInstance, classInstance.tearDown)
    end

    self:endTest()
end

LuaUnit.TEST_FUNCTION_PREFIX = '<TestFunction>'

function LuaUnit:_printTestMethod(classPath, classInstance, methodName,
                                  methodInstance)
    local className = table.concat(classPath, '.')
    if className == '' then
        className = self.TEST_FUNCTION_PREFIX
    end
    print(className .. ':' .. methodName)
end

function LuaUnit:runSomeTest(classPath, classInstance,
                             methodName, methodInstance, run_cb)
    assert(type(classInstance) == 'table')
    if methodInstance then
        assert(type(methodInstance) == 'function')
        run_cb(self, classPath, classInstance, methodName, methodInstance)
        return
    end
    local tests = {}

    local function add(name, value)
        if type(value) == 'table' or type(value) == 'function' then
            tests[name] = value
        end
    end

    for name, value in pairs(classInstance) do
        if string.match(string.lower(name), '^test') then
            add(name, value)
            tests[name] = value
        end
    end

    if classInstance._tests then
        for name, value in pairs(classInstance._tests) do
            add(name, value)
        end
    end

    local cpCopy = pl.tablex.copy(classPath)
    local n = #cpCopy
    for name, value in pl.tablex.sort(tests) do
        if type(value) == 'function' then
            self:runSomeTest(classPath, classInstance, name, value, run_cb)
        else
            cpCopy[n + 1] = name
            self:runSomeTest(cpCopy, value, nil, nil, run_cb)
        end
    end
end

function LuaUnit:add_test(name, value, parent)
    parent = parent or _G
    if string.find(name, '[:%.]') then
        error("'.' and ':' are not allowed in test names")
    end
    if type(value) ~= 'function' and type(value) ~= 'table' then
        error('Test implementation must be function or table')
    end
    parent._tests = parent._tests or {}
    if parent._tests[name] then
        error('Duplicate test name: ' .. name)
    end
    parent._tests[name] = value
end

function LuaUnit:main(...)
    local result = self:run(...)
    assert(type(result) == 'number')
    if result == 0 then
        os.exit(0)
    else
        os.exit(1)
    end
end

function LuaUnit:run(...)
    -- Run some specific test classes.
    -- If no arguments are passed, run the class names specified on the
    -- command line. If no class name is specified on the command line
    -- run all classes whose name starts with 'Test'
    --
    -- If arguments are passed, they must be strings of the class names
    -- that you want to run
    local outputType = os.getenv("LUAUNIT_OUTPUT_TYPE")
    if outputType then LuaUnit:setOutputType(outputType) end

    local runner = self()
    local list_tests = os.getenv("LUAUNIT_LIST_TESTS")
    if list_tests then
        runner:listTests()
        return 0
    else
        return runner:runSuite(...)
    end
end

function LuaUnit:listTests()
    self:runSomeTest({}, _G, nil, nil, self._printTestMethod)
end

local function parse_test_name(name)
    local class_name, method_name
    local method_sep = string.find(name, ':')
    if method_sep then
        class_name = string.sub(name, 1, method_sep - 1)
        if class_name == LuaUnit.TEST_FUNCTION_PREFIX then
            class_name = ''
        end
        method_name = string.sub(name, method_sep + 1)
    else
        class_name = name
    end

    return pl.stringx.split(class_name, '.'), method_name
end

local function get_child(obj, p)
    if obj[p] then return obj[p] end
    if obj._tests and obj._tests[p] then return obj._tests[p] end
    error('Invalid child' .. p)
end

function LuaUnit:runSuite(...)
    self:startSuite()

    args={...}
    if #args == 0 then
        args = arg
    end

    if #args ~= 0 then
        for _, name in ipairs(args) do
            local classPath, methodName = parse_test_name(name)
            local obj = _G
            for _, p in ipairs(classPath) do
                obj = get_child(obj, p)
            end
            local methodInstance
            if methodName then
                methodInstance = get_child(obj, methodName)
            end

            self:runSomeTest(classPath, obj, methodName, methodInstance,
                             self._runTestMethod)
        end
    else
        self:runSomeTest({}, _G, nil, nil, self._runTestMethod)
    end

    return self.result.failureCount
end

-- class LuaUnit

