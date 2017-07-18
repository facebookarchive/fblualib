# FBLuaLib: installation

## One-step installation instructions

If you're on Ubuntu 13.10 or Ubuntu 14.04 (or higher), you can use the
one-stop installation script:

```
curl -sk https://raw.githubusercontent.com/facebook/fblualib/master/install_all.sh | bash
```

This will install [folly](https://github.com/facebook/folly),
[fbthrift](https://github.com/facebook/fbthrift), [Torch](https://torch.ch),
[TH++](https://github.com/facebook/thpp), and FBLuaLib.

**NOTE** You might need to reinstall Torch even if you already have it installed, as older
versions do not install LuaJIT with Lua 5.2 compatibility. To check, run
`luajit -e ';;'` -- if you get an error ("unexpected symbol near ';'"),
then you need to reinstall. 

## Detailed installation instructions

FBLuaLib is composed of a few separate (but interdependent) packages.

1. Install [TH++](https://github.com/facebook/thpp) by following
[the instructions](https://github.com/facebook/thpp/blob/master/INSTALL.md)
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
