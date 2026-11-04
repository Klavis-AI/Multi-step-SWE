local function f(...) return select("#", ...), ... end
print(f())
print(f(1, nil, 3))
local function g(a, ...) return a, select("#", ...) end
print(g(1, 2, 3))
local function h(...) local t = {...} return #t end
print(h(1, 2, 3))
