-- Copyright 2004-present Facebook. All Rights Reserved.

local Stats = {
  DEFAULT_DECAY_FACTOR = 2
}

function Stats.new(name, decay_factor)
  local self = {
    name = name or nil,
    last_value = 0,
    avg = 0,
    stddevsqr = 0,
    n = 0,
    min = math.huge,
    max = -math.huge,
    total = 0,
    decay_factor = decay_factor or Stats.DEFAULT_DECAY_FACTOR,
    davg = 0,
    decaying_stddevsqr = 0,
  }
  setmetatable(self, {__index = Stats})
  return self
end

function Stats:add(value)
  self.last_value = value
  self.total = self.total + value
  -- see http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
  self.n = self.n + 1;
  local delta = value - self.avg
  self.avg = self.avg + delta / self.n
  local md = 1 / self.decay_factor
  if (self.n == 1) then
    self.davg = value
  end

  self.davg = (md * self.davg + (1 - md) * value);

  -- This expression uses the new value of mean
  self.stddevsqr = self.stddevsqr + delta * (value - self.avg);
  self.decaying_stddevsqr =
    self.decaying_stddevsqr + delta * (value - self.davg)

  self.max = math.max(self.max, value);
  self.min = math.min(self.min, value);
end

function Stats:get_variance()
  if self.n > 1 then
    return math.sqrt(self.stddevsqr / (self.n - 1));
  else
    return 0
  end
end

function Stats:get_dvariance()
  if self.n > 1 then
    return math.sqrt(self.decaying_stddevsqr / (self.n - 1));
  else
    return 0
  end
end

function Stats:reset()
  self.last_value = 0
  self.avg = 0
  self.stddevsqr = 0
  self.n = 0
  self.min = math.huge
  self.max = -math.huge
  self.total = 0
  self.davg = 0
  self.decaying_stddevsqr = 0
end

return Stats
