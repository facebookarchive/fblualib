require('fb.luaunit')
local _debugger  = require('fb.debugger')
local Stats = require('fb.util.stats')


-- This is cut-and-pasta from the original lua-unit which we forked.
-- Task #5814467 is to move the new assert methods from the open-source
-- version into our replica.
local function assertAlmostEquals(actual, expected, margin)
  -- check that two floats are close by margin
  if type(actual) ~= 'number'
    or type(expected) ~= 'number'
    or type(margin) ~= 'number' then
    error('assertAlmostEquals: must supply only number arguments.' ..
      '\nArguments supplied: ' .. actual .. ', ' .. expected ..
      ', ' .. margin, 2)
  end
  if margin < 0 then
    error('assertAlmostEquals: margin must be positive, current value is '
      .. margin, 2)
  end

  -- help lua in limit cases like assertAlmostEquals( 1.1, 1.0, 0.1)
  -- which by default does not work. We need to give margin a small boost
  local realmargin = margin + 0.00000000001
  if math.abs(expected - actual) > realmargin then
    error( 'Values are not almost equal\nExpected: ' .. expected ..
      ' with margin of ' .. margin .. ', received: ' .. actual, 2)
  end
end

Test = {
  EPS = 0.0001
}

function Test:testDefaultNew()
  local s = Stats.new()
  assertEquals(Stats.DEFAULT_DECAY_FACTOR, s.decay_factor);
end

function Test:testNewWithParams()
  local decay = 8.2
  local s = Stats.new("name", decay)
  assertEquals(decay, s.decay_factor)
  assertEquals("name", s.name)
end

function Test:testAddValue()
  local s = Stats.new()
  s:add(10)
  assertEquals(10, s.last_value)
  assertEquals(10, s.min)
  assertEquals(10, s.max)
  assertEquals(1, s.n)
end

function Test:testAvg()
  local s = Stats.new()
  s:add(10)
  s:add(20)
  assertEquals(15, s.avg)
end

function Test:testGetDAverage()
  local s = Stats.new()
  s:add(10)
  assertEquals(10, s.davg)
  s:add(10)
  assertEquals(10, s.davg)
  s:add(20)
  assertEquals(15, s.davg)
  s:add(20)
  assertEquals(17.5, s.davg)
end

function Test:testMin()
  local s = Stats.new()
  s:add(10)
  assertEquals(10, s.min)
  s:add(20)
  assertEquals(10, s.min)
  s:add(-20)
  assertEquals(-20, s.min)
end

function Test:testMax()
  local s = Stats.new()
  s:add(10)
  assertEquals(10, s.max)
  s:add(20)
  assertEquals(20, s.max)
  s:add(-20)
  assertEquals(20, s.max)
end

function Test:testVariance()
  local s = Stats.new()
  s:add(10)
  assertEquals(0, s:get_variance())
  s:add(10)
  assertEquals(0, s:get_variance())
  s:add(20)
  assertAlmostEquals(5.7735, s:get_variance(), Test.EPS)
end

function Test:testVariance()
  local s = Stats.new()
  s:add(10)
  assertEquals(0, s:get_dvariance())
  s:add(10)
  assertEquals(0, s:get_dvariance())
  s:add(20)
  assertAlmostEquals(5, s:get_dvariance(), Test.EPS)
  s:add(20)
  assertAlmostEquals(4.714, s:get_dvariance(), Test.EPS)
end

function Test:testN()
  local s = Stats.new()
  assertEquals(0, s.n)
  s:add(10)
  assertEquals(1, s.n)
  s:add(10)
  assertEquals(2, s.n)
end

function Test:testLastValue()
  local s = Stats.new()
  assertEquals(0, s.last_value)
  s:add(10)
  assertEquals(10, s.last_value)
  s:add(20)
  assertEquals(20, s.last_value)
end

function Test:testReset()
  local s = Stats.new()
  s:add(10)
  s:reset()
  assertEquals(0, s.n)
  assertEquals(0, s.avg)
  assertEquals(0, s.davg)
  assertEquals(0, s:get_dvariance())
  assertEquals(math.huge, s.min)
  assertEquals(-math.huge, s.max)
end

function Test:testTotal()
  local s = Stats.new()
  assertEquals(0, s.total)
  s:add(10)
  assertEquals(10, s.total)
  s:add(5)
  assertEquals(15, s.total)
end

LuaUnit:main()
