-- Copyright 2004-present Facebook. All Rights Reserved.

require 'fb.luaunit'

local torch = require('fbcode.deeplearning.torch.fbtorch')
local find = require('fb.util.find')

function testfind1d()
    local dim1 = math.random(10,1000)
    local a = torch.randn(dim1)
    local id = find(a:ge(0))
    local b = a:index(1,id)
    local s1 = b:sum()
    local ap = (a+torch.abs(a))/2
    local s2 = ap:sum()
    assertEquals( s1, s2 )
end

function testfind2d()
    local dim1 = math.random(10,1000)
    local dim2 = math.random(10,1000)
    local a = torch.randn(dim1,dim2)
    local rowid = math.random(1,dim1)
    local colid = math.random(1,dim2)

    local idc1 = find(a:ge(0)[{{},{colid}}]:squeeze())
    local idc2 = find(a[{{},{colid}}]:ge(0):squeeze())

    local idr1 = find(a:ge(0)[{{rowid},{}}]:squeeze())
    local idr2 = find(a[{{rowid},{}}]:ge(0):squeeze())


    local col = a[{{},{colid}}]:squeeze():clone()
    local row = a[{{rowid},{}}]:squeeze():clone()

    local poscol1 = col:index(1, idc1)
    local poscol2 = col:index(1, idc2)
    local posrow1 = row:index(1, idr1)
    local posrow2 = row:index(1, idr2)
    local poscolsum1 = poscol1:sum()
    local poscolsum2 = poscol2:sum()
    local posrowsum1 = posrow1:sum()
    local posrowsum2 = posrow2:sum()

    local colp = (col + torch.abs(col))/2
    local rowp = (row + torch.abs(row))/2

    assertEquals( posrowsum1, rowp:sum() )
    assertEquals( posrowsum2, rowp:sum() )
    assertEquals( poscolsum1, colp:sum() )
    assertEquals( poscolsum2, colp:sum() )
end
-- run a bunch of times
for i = 1,100 do
    testfind1d()
    testfind2d()
end

LuaUnit:main()
