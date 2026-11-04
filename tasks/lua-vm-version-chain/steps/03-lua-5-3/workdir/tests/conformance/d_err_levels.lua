local function thrower() error("deep", 2) end
local function caller() thrower() end
print(pcall(caller))
local function l1() error("lvl1", 1) end
print(pcall(l1))
print(pcall(function() error("lvl3", 3) end))
