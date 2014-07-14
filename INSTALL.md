# FBLuaLib: installation

FBLuaLib is composed of a few separate (but interdependent) packages.

1. Install [TH++](https://github.com/facebook/thpp) by following
[the instructions](https://github.com/facebook/thpp/INSTALL.md)
2. Install a few additional packages; on Ubuntu 13.10 and 14.04, they are as
follows (feel free to cut and paste the apt-get command below):
```
sudo apt-get install \
  libedit-dev \
  libmatio-dev \
  libpython-dev \
  python-numpy
```
2. Build and install FBLuaLib; `cd fblualib; ./build.sh` or follow the steps
in the script yourself.
4. Try it out! run the default Torch shell (`th`) or, to use
[fb.repl](fblualib/trepl/README.md), `luajit -e "require('fb.trepl').repl()"`
