# fblualib: A collection of Lua / Torch utilities

FBLuaLib is a collection of Lua / Torch utilities developed at Facebook that
we've found useful. Some of these utilities are useful without Torch.

[LuaJIT](http://luajit.org/) is required, and we currently only support
x86_64 Linux.

* [C++ LuaUtils](fblualib/README.md) is a collection of C++ utilities useful
  for writing Lua extensions
* [fb.util](fblualib/util/README.md) is a collection of low-level Lua utilities
  that, in addition to being useful on their own, are depended upon by
  everything else. Does not require Torch.
* [fb.editline](fblualib/editline/README.md) is a command line editing library
  based on [libedit](http://thrysoee.dk/editline/). Does not require Torch.
* [fb.trepl](fblualib/trepl/README.md) is a configurable Read-Eval-Print loop
  with line editing and autocompletion. Does not require Torch (but has
  Torch-specific features if Torch is installed)
* [fb.debugger](fblualib/debugger/README.md) is a full-featured source-level
  Lua debugger. Does not require Torch.
* [fb.python](fblualib/python/README.md) is a bridge between Lua and Python,
  allowing seamless integration between the two (enabling, for example,
  using [SciPy](http://www.scipy.org/) with Lua tensors almost as efficiently
  as with native `numpy` arrays; data between Lua tensors and the corresponding
  Python counterpart `numpy.ndarray` objects is shared, not copied). Requires
  Torch.
* [fb.thrift](fblualib/thrift/README.md) is a library for fast serialization
  of arbitrary Lua objects using [Thrift](https://github.com/facebook/fbthrift).
  Requires Torch.
* [fb.mattorch](fblualib/mattorch/README.md) is a library for reading
  and writing [Matlab](http://www.mathworks.com/products/matlab/) `.mat` files 
  from Torch without having Matlab installed.
