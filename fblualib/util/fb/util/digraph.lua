--
--  Copyright (c) 2014, Facebook, Inc.
--  All rights reserved.
--
--  This source code is licensed under the BSD-style license found in the
--  LICENSE file in the root directory of this source tree. An additional grant
--  of patent rights can be found in the PATENTS file in the same directory.
--

-- Directed graph class

local pl = require('pl.import_into')()
local module_name = ...

local M = {}

local Digraph = pl.class()

-- Create a new (empty) digraph
function Digraph:_init()
    self:clear()
end

-- Add a vertex v to a digraph
function Digraph:add_vertex(v)
    if self._out_edges[v] then
        error('Duplicate vertex')
    end
    self._vertex_count = self._vertex_count + 1
    self._sources[v] = true
    self._sinks[v] = true
    self._out_edges[v] = {}
    self._in_edges[v] = {}
end

-- Returns true if the given vertex exists; false if not
function Digraph:has_vertex(v)
    return self._out_edges[v] ~= nil
end

-- Add an edge from v to w to the digraph; the vertices must already exist,
-- but the edge must not.
function Digraph:add_edge(v, w, e)
    e = e or true
    if not self:has_vertex(v) then
        error('Source vertex does not exist')
    end
    if not self:has_vertex(w) then
        error('Destination vertex does not exist')
    end
    if self._out_edges[v][w] then
        error('Duplicate edge')
    end
    self._edge_count = self._edge_count + 1
    self._out_edges[v][w] = e
    self._in_edges[w][v] = e
    self._sinks[v] = nil
    self._sources[w] = nil
end

-- Returns associated edge data for an edge if it exists
function Digraph:get_edge(v, w)
    if not self:has_vertex(v) or not self:has_vertex(w) then
        return nil
    end
    return self._out_edges[v][w]
end

-- Remove an edge from v to w in the digraph; the edge must exist.
function Digraph:remove_edge(v, w)
    if (not self:has_vertex(v)) or (not self:has_vertex(w)) then
        error('Vertex does not exist')
    end
    if not self._out_edges[v][w] then
        error('Edge does not exist')
    end
    self._out_edges[v][w] = nil
    self._in_edges[w][v] = nil
    if not next(self._out_edges[v]) then
        self._sinks[v] = true
    end
    if not next(self._in_edges[w]) then
        self._sources[w] = true
    end
    self._edge_count = self._edge_count - 1
end

-- Remove a vertex (and all edges to and from it) from the digraph.
function Digraph:remove_vertex(v)
    if not self:has_vertex(v) then
        error('Vertex does not exist')
    end
    while true do
        local w = next(self._out_edges[v])
        if not w then
            break
        end
        self:remove_edge(v, w)
    end
    while true do
        local w = next(self._in_edges[v])
        if not w then
            break
        end
        self:remove_edge(w, v)
    end
    self._out_edges[v] = nil
    self._in_edges[v] = nil
    self._sources[v] = nil
    self._sinks[v] = nil
    self._vertex_count = self._vertex_count - 1
end

local keys = pl.tablex.keys

local function vkeys(set)
    if not set then
        error('Vertex does not exist')
    end
    return keys(set)
end

local function vset(set)
    if not set then
        error('Vertex does not exist')
    end
    return set
end

-- Return a list of all predecessors of v (nodes w such that the graph contains
-- an edge from w to v)
function Digraph:predecessors(v)
    return vkeys(self._in_edges[v])
end

function Digraph:in_edges(v)
    return vset(self._in_edges[v])
end

-- Return a list of all successors of v (nodes w such that the graph contains
-- an edge from v to w)
function Digraph:successors(v)
    return vkeys(self._out_edges[v])
end

function Digraph:out_edges(v)
    return vset(self._out_edges[v])
end

-- Return a list of all sources (nodes with no predecessors)
function Digraph:sources()
    return keys(self._sources)
end

-- Return a list of all sinks (nodes with no successors)
function Digraph:sinks()
    return keys(self._sinks)
end

-- Return the number of vertices
function Digraph:vertex_count()
    return self._vertex_count
end

-- Return the number of edges
function Digraph:edge_count()
    return self._edge_count
end

-- Clear the graph
function Digraph:clear()
    self._vertex_count = 0
    self._edge_count = 0
    self._out_edges = {}
    self._in_edges = {}
    self._sources = {}
    self._sinks = {}
end

-- Internal core function that does most of the work of merge and clone
function Digraph:_merge(other, edges, clone_vertex, clone_edge)
    local map = {}
    for v, _ in pairs(edges) do
        local nv
        if clone_vertex then
            nv = clone_vertex(v)
            map[v] = nv
        else
            nv = v
        end
        self:add_vertex(nv)
    end
    for v, ws in pairs(edges) do
        for w, e in pairs(ws) do
            if clone_vertex then
                v = map[v]
                w = map[w]
            end
            if clone_edge then
                e = clone_edge(e)
            end
            self:add_edge(v, w, e)
        end
    end
end

-- Merge another digraph into this; the sets of vertices must be disjoint.
-- other will be cleared.
function Digraph:merge(other)
    self:_merge(other, other._out_edges)
    other:clear()
end

-- Internal helper function for clone
function Digraph:_clone(edges, clone_vertex, clone_edge)
    local copy = Digraph()
    copy:_merge(self, edges, clone_vertex, clone_edge)
    return copy
end

-- Create a clone of the graph. If clone_fn is specified, all nodes are cloned
-- using clone_fn, otherwise they are the same objects as in this graph.
function Digraph:clone(clone_vertex, clone_edge)
    return self:_clone(self._out_edges, clone_vertex, clone_edge)
end

-- Return a clone of the graph with all edges reversed. clone_fn has the same
-- meaning as for clone.
function Digraph:reversed(clone_vertex, clone_edge)
    return self:_clone(self._in_edges, clone_vertex, clone_edge)
end

-- Helper function for topological sorting; destroys the graph.
function Digraph:_destructive_topo_sort()
    local sorted = {}
    while true do
        local v = next(self._sources)
        if not v then
            break
        end
        table.insert(sorted, v)
        self:remove_vertex(v)
    end
    if self._vertex_count ~= 0 then
        error('Digraph has cycles')
    end
    return sorted
end

-- Return a topologically sorted list of nodes (if B can be reached from A
-- by traversing only forward edges, then A will appear before B in the list);
-- throws an error if the graph contains cycles.
function Digraph:topo_sort()
    return self:clone():_destructive_topo_sort()
end

-- Return a reverse topological sorted list of nodes (if A can be reached from
-- B by traversing only forward edges, then A will appear before B in the list);
-- throws an error if the graph contains cycles.
function Digraph:reverse_topo_sort()
    return self:reversed():_destructive_topo_sort()
end

-- Return the number of nodes
function Digraph:__len()
    return self._vertex_count
end

function Digraph:all()
    return self._out_edges, self._in_edges
end

-- Join two graphs, connecting every sink in this to every source in other.
function Digraph:cross(other, e)
    local vs, ws = self:sinks(), other:sources()  -- make a copy

    self:merge(other)

    for _, v in ipairs(vs) do
        for _, w in ipairs(ws) do
            self:add_edge(v, w, e)
        end
    end
end

function Digraph:for_each(func)
    for v, _ in pairs(self._out_edges) do
        func(v)
    end
end

M.Digraph = Digraph

local ok, thrift = pcall(require, 'fb.thrift')
if ok then
    thrift.add_penlight_classes(M, module_name)
end

return M
