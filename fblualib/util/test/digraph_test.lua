--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

local digraph = require('fb.util.digraph')
local pl = require('pl.import_into')()

require('fb.luaunit')

local function sorted(t)
    return pl.List(t):sort()
end

TestDigraph = {}

function TestDigraph:setUp()
    self.G = digraph.Digraph()
    for i = 1, 10 do
        self.G:add_vertex(i)
    end
    for i = 1, 9 do
        self.G:add_edge(i + 1, i, i * 10)
    end
end

function TestDigraph:testBasic()
    local G = self.G

    assertEquals(10, #G)
    for i = 1, 10 do
       assertTrue(G:has_vertex(i))
    end
    assertFalse(G:has_vertex(11))

    assertEquals(10, G:vertex_count())
    assertEquals(9, G:edge_count())
    assertEquals({10}, G:sources())
    assertEquals({1}, G:sinks())
    assertTrue(pl.tablex.compare_no_order(
        {[4] = 40},
        G:out_edges(5)))
    assertTrue(pl.tablex.compare_no_order(
        {[5] = 40},
        G:in_edges(4)))

    G:add_edge(1, 3)
    assertEquals({}, G:sinks())

    assertEquals({2}, G:predecessors(1))
    assertEquals({3}, sorted(G:successors(1)))
    assertEquals({1, 4}, sorted(G:predecessors(3)))
    assertEquals({2}, G:successors(3))
end

function TestDigraph:testRemoveVertex()
   local G = self.G

   for i = 10, 1, -1 do
      assertTrue(G:has_vertex(i))
      if i > 1 then
         assertEquals(G:get_edge(i, i - 1), (i - 1) * 10)
      end

      G:remove_vertex(i)
      assertFalse(G:has_vertex(i))

      if i > 1 then
         assertEquals(G:get_edge(i, i - 1), nil)
      end
   end
end

function TestDigraph:testForEach()
    local G = self.G
    local seen = {}

    G:for_each(function(v) seen[v] = (seen[v] or 0) + 1 end)
    assertEquals(pl.Set(seen), pl.Set({1, 1, 1, 1, 1, 1, 1, 1, 1, 1}))
end

function TestDigraph:testTopoSort1()
    local G = self.G

    assertEquals({10, 9, 8, 7, 6, 5, 4, 3, 2, 1}, G:topo_sort());
    assertEquals({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, G:reverse_topo_sort());

    G:add_edge(1, 5);

    assertError(G.topo_sort, G);
    assertError(G.reverse_topo_sort, G);
end

function TestDigraph:testClone()
    local G = self.G:clone(
        function(v) return string.format('v%d', v) end,
        function(e) return string.format('e%d', e) end)

    assertEquals(10, #G)
    assertEquals(10, G:vertex_count())
    assertEquals(9, G:edge_count())
    assertEquals({'v10'}, G:sources())
    assertEquals({'v1'}, G:sinks())
    assertTrue(pl.tablex.compare_no_order(
        {v4 = 'e40'},
        G:out_edges('v5')))
    assertTrue(pl.tablex.compare_no_order(
        {v5 = 'e40'},
        G:in_edges('v4')))
    G:add_edge('v1', 'v3')
    assertEquals({}, G:sinks())

    assertEquals({'v2'}, G:predecessors('v1'))
    assertEquals({'v3'}, sorted(G:successors('v1')))
    assertEquals({'v1', 'v4'}, sorted(G:predecessors('v3')))
    assertEquals({'v2'}, G:successors('v3'))
end

function testCross()
    local G1 = digraph.Digraph()

    local cross_from = {}
    for i = 1, 10 do
        table.insert(cross_from, i + 10)
        G1:add_vertex(i)
        G1:add_vertex(i + 10)
        G1:add_edge(i, i + 10)
    end
    assertEquals(20, #G1)

    local cross_to = {}

    local G2 = digraph.Digraph()
    for i = 101, 110 do
        table.insert(cross_to, i)
        G2:add_vertex(i)
        G2:add_vertex(i + 10)
        G2:add_edge(i, i + 10)
    end
    assertEquals(20, #G2)

    G1:cross(G2)

    assertEquals(40, #G1)
    assertEquals(0, #G2)

    for i = 1, 10 do
        assertEquals({}, G1:predecessors(i))
        assertEquals({i + 10}, G1:successors(i))
        assertEquals({i}, G1:predecessors(i + 10))
        assertEquals(cross_to, sorted(G1:successors(i + 10)))
        assertEquals(cross_from, sorted(G1:predecessors(i + 100)))
        assertEquals({i + 110}, G1:successors(i + 100))
        assertEquals({i + 100}, G1:predecessors(i + 110))
        assertEquals({}, G1:successors(i + 110))
    end
end

function testForEachCycle()
    local G = digraph.Digraph()

    G:add_vertex(1)
    G:add_vertex(2)
    G:add_edge(1, 2)
    G:add_edge(2, 1)

    local seen = {}
    G:for_each(function(v) seen[v] = (seen[v] or 0) + 1 end)

    assertEquals(pl.Set(seen), pl.Set({1, 1}))
end

LuaUnit:main()
