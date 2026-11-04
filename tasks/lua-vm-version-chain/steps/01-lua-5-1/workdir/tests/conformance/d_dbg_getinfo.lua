local function f() end
local i = debug.getinfo(f)
print(i.what, i.short_src ~= nil, i.linedefined > 0, i.nups)
local c = debug.getinfo(print)
print(c.what, c.short_src, c.currentline)
print(debug.getinfo(1).what)
print(tostring(debug.getinfo(100)))
