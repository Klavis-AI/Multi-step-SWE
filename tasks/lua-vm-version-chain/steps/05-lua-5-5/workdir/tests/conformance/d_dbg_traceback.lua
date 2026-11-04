local function inner() return debug.traceback("MSG", 1) end
local function outer() return inner() end
local tb = outer()
print((tb:gsub("0x%x+", "PTR")))
print(debug.traceback("only", 100))
print(type(debug.traceback()))
