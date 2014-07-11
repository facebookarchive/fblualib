# fb-mattorch: Read and write Matlab files (without Matlab)

This is an alternative to the
[mattorch](https://github.com/clementfarabet/lua---mattorch) package that
provides a subset of functionality (it only permits reading and writing
of Matlab files) but does not require that you have Matlab installed;
it uses the open-source [MAT File I/O Library](http://matio.sourceforge.net/)
instead.

## Usage

```lua
local mattorch = require('fb.mattorch')

-- Load all variables from a Matlab file; returns a table of the form
-- {name = value}, where values are torch Tensors
local vars = mattorch.load('foo.mat')

-- Save all variables from a Matlab file; given a table of the form
-- {name = value}, where values are torch Tensors, writes them to
-- a Matlab file
mattorch.save('bar.mat', vars)

-- Save one single tensor to a Matlab file under the uninspired name 'x'
-- equivalent to mattorch.save('baz.mat', {x = var})
mattorch.save('baz.mat', var)
```
