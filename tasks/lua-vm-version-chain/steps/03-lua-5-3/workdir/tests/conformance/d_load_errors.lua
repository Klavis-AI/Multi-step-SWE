local L = load
if _VERSION == "Lua 5.1" then L = loadstring end
print(L("return +"))
-- the name reported here is whichever alias the search reaches first
local okN, eN = pcall(L, nil)
print(okN, (tostring(eN):gsub("to '[%w_.]+'", "to '?'")))
local parts = {"return ", "1 + ", "2"}
local i = 0
local f = load(function() i = i + 1 return parts[i] end)
print(f and f() or "reader-form unsupported")
print(tostring(loadfile("nosuchfile.lua")))
print(pcall(dofile, "nosuchfile.lua"))
