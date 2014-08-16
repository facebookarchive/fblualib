# fb.torch: Torch-specific extensions

## __AsyncRNG__, an asynchronous random number generator.

AsyncRNG is a mechanism for generating (pseudo) random numbers from a
different thread, in case your application requires a lot of random
numbers quickly.

### Usage

```lua
local async_rng = require('fb.torch.async_rng')
```

You may create random number generators that generate numbers according
to any one of the
[C++11 distributions](http://en.cppreference.com/w/cpp/numeric/random).
The underlying generator is `mt19937` (or the 64-bit version, `mt19937_64`)
seeded with entropy from the system random device (`/dev/urandom` on Linux).

```lua
local rng = async_rng.distribution(tensor_type, chunk_size, ...)
```

* `distribution` is the distribution as shown on the C++11 distributions page,
above; one of `uniform`, `uniform_int`, `bernoulli`, `binomial`,
`negative_binomial`, `geometric`, `poisson`, `exponential`, `gamma`,
`weibull`, `extreme_value`, `normal`, `lognormal`, `chi_squared`, `cauchy`,
`fisher_f`, `student_t`, `discrete`, `piecewise_constant`, `piecewise_linear`.
* `tensor_type` is the type of generated tensors, either `torch.FloatTensor`
or `torch.DoubleTensor`.
* `chunk_size` is the number of random numbers to generate ahead of time from
another thread; reads of `chunk_size` numbers or fewer will usually not
block.
* The remaining arguments are distribution-specific parameters, see
below.

### Distributions

* [uniform](http://en.cppreference.com/w/cpp/numeric/random/uniform_real_distribution) produces random numbers uniformly distributed
on the interval `[a, b)`. Parameters: `a` (default 0), `b` (default 1).
* [uniform_int](http://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution) produces random integers uniformly distributed
on the closed interval `[a, b]`. Parameters: `a`, `b`, both are required.
* [bernoulli](http://en.cppreference.com/w/cpp/numeric/random/bernoulli_distribution) produces random 0 or 1 values according to a
discrete probability function. The probability of 1 is `p`, the probability of
0 is `1 - p`. Parameters: `p` (default 0.5).
* [binomial](http://en.cppreference.com/w/cpp/numeric/random/binomial_distribution) produes random non-negative integers according to
the binomial probability distribution. The value returned is the number of
successes in a sequence of `t` yes/no experiments, each of which succeeds
with probability `p`. Parameters: `t` (default 1), `p` (default 0.5).
* [negative_binomial](http://en.cppreference.com/w/cpp/numeric/random/negative_binomial_distribution) produces random non-negative integers
according to the negative binomial probability distribution. The value returned
is the number of failures in a series of yes/no experiments, each of
which succeeds with probability `p`, before exactly `k` successes occur.
Parameters: `k` (default 1), `p` (default 0.5).
* [geometric](http://en.cppreference.com/w/cpp/numeric/random/geometric_distribution) produces random non-negative integers according
to the geometric probability distribution. The value returned represents
the number of yes/no trials (each succeeding with probability `p`) which
are necessary to obtain a single success. Parameters: `p` (default 0.5).
* [poisson](http://en.cppreference.com/w/cpp/numeric/random/poisson_distribution) produces random non-negative integers according to
the Poisson probability distribution. The probability of returning `i` is
the probability of exactly `i` occurrences of a random event if the expected
(mean) number of occurrences is `m`. Parameters: `m` (default 1).
* [exponential](http://en.cppreference.com/w/cpp/numeric/random/exponential_distribution) produces random non-negative floating point values
according to the exponential probability distirbution. The value returned
is the time/distance until the next random event, if random events occur
at a constant rate of `lambda` events per unit of time/distance. Parameters:
`lambda` (default 1).
* [gamma](http://en.cppreference.com/w/cpp/numeric/random/gamma_distribution)
produces random positive floating point values, distributed according to the
gamma distribution. `alpha` is the _shape_ parameter and `beta` is the _scale_ parameter; for floating point `alpha`, the value returned is the sum of
`alpha` indepedent exponentially distributed random variables, each of
which has a mean of `beta`. Parameters: `alpha` (default 1), `beta` (default 1).
* [weibull](http://en.cppreference.com/w/cpp/numeric/random/weibull_distribution) produces random numbers according to the [Weibull distribution](http://en.wikipedia.org/wiki/Weibull_distribution), with
_shape_ parameter `alpha` and _scale_ parameter `beta`. Parameters:
`alpha` (default 1), `beta` (default 1).
* [extreme_value](http://en.cppreference.com/w/cpp/numeric/random/extreme_value_distribution) produces random numbers according to the
[extreme value distribution](http://en.wikipedia.org/wiki/Generalized_extreme_value_distribution). Parameters: `a` (default 0),
`b` (default 1).
* [normal](http://en.cppreference.com/w/cpp/numeric/random/normal_distribution) produces random numbers according to the normal distribution with mean
`mu` and standard deviation `sigma`. Parameters: `mu` (default 0),
`sigma` (default 1).
* [lognormal](http://en.cppreference.com/w/cpp/numeric/random/lognormal_distribution) produces random numbers according to the log-normal
distribution with mean `mu` and standard deviation `sigma`. Parameters:
`mu` (default 0), `sigma` (default 1).
* [chi_squared](http://en.cppreference.com/w/cpp/numeric/random/chi_squared_distribution) produces random numbers according to the Chi-squared
distribution with `n` degrees of freedom. Parameters: `n` (default 1).
* [cauchy](http://en.cppreference.com/w/cpp/numeric/random/cauchy_distribution) produces random numbers according to the Cauchy distribution. Parameters:
`a` (default 0), `b` (default 1).
* [fisher_f](http://en.cppreference.com/w/cpp/numeric/random/fisher_f_distribution) produces random numbers according to the f-distribution
with `m` and `n` degrees of freedom. Parameters: `m` (default 1), `n`
(default 1).
* [student_t](http://en.cppreference.com/w/cpp/numeric/random/student_t_distribution) produces random numbers according to the t-distribution
with `n` degrees of freedom. Parameters: `n` (default 1).
* [discrete](http://en.cppreference.com/w/cpp/numeric/random/discrete_distribution/discrete_distribution) produces random integers on
the interval `[0, n)`, where the probabilities of each individual integer
`i` is defined as `w / S`, where `w` is the _weight_ corresponding to integer
`i`, and `S` is the sum of all weights. Parameters: a list of `n` non-negative
floating point values (the weights).
* [piecewise_constant](http://en.cppreference.com/w/cpp/numeric/random/piecewise_constant_distribution) produces random floating point numbers which
are uniformly distributed within each of the `n` subintervals `[b[i], b[i+1])`,
each with its own weight `w[i]` (normalized by the sum of all weights).
Parameters: a list of `n + 1` values representing the interval boundaries `b`
and a list of `n` values representing the weights `w`. Default `n` is 1, `b`
is `{0, 1}`, `w` is `{1}`, and the distribution becomes a uniform distribution
on the interval `[0, 1)`.
* [piecewise_linear](http://en.cppreference.com/w/cpp/numeric/random/piecewise_linear_distribution) produces random floating point numbers,
distributed according to a linear probability density function on each of the
`n` subintervals `[b[i], b[i+1])`; the distribution is such as the probability
density at each boundary `b[i]` is `p[i]`, where `p[i]` is the corresponding
weight `w[i]`, normalized (divided by the sum of all `(w[k] + w[k-1]) *
(b[k+1] - b[k]) / 2`). Parameters: a list of `n + 1` values representing
the interval boundaries `b` and a list of `n + 1` values representing the weights. Default `n` is 1, `b` is `{0, 1}`, and `w` is `{1, 1}`.

Note that the `discrete` distributions produces integers on `[0, n)`,
so you must add 1 to these values if you intend to sample from a Lua array,
as Lua uses 1-based indices.

### Example

```lua
local async_rng = require('fb.torch.async_rng')

-- 1e6 is the chunk size; the number of random numbers that we keep on hand
-- at all times; reads of less than these many numbers will usually not block;
-- see comments in the code for more details.

-- This generates uniformly distributed floats between 1 (inclusive) and 7
-- (exclusive)
local uniform = async_rng.uniform('torch.FloatTensor', 1e6, 1, 7)

-- This is a Bernoulli distribution; it generates 1 with a probability of 0.3
-- and 0 with a probability of 0.7
local bernoulli = async_rng.bernoulli('torch.FloatTensor', 1e6, 0.3)

-- Generate 400 random numbers; 
local batch = uniform:generate(400)

-- Note batch returns a *list* of 1d tensors totalling 400 elements
local n = 0
for _, tensor in ipairs(batch) do
  -- process tensor
  n = n + tensor:nElement()
end
assert(n == 400)
```
