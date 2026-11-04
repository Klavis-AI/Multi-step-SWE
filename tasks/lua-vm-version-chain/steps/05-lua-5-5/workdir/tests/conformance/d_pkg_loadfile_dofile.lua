local f = assert(loadfile("mymod.lua"))
local m = f()
print(type(m), m.name)
print(dofile("mymod.lua").twice(5))
print(tostring(loadfile("nope.lua")))
package.preload["pre"] = function() return "from preload" end
print(require("pre"))
