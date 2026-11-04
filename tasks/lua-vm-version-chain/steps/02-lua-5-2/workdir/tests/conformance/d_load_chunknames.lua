local L = load
if _VERSION == "Lua 5.1" then L = loadstring end
local f = L("error('x')", "=myname")
print(pcall(f))
local g = L("error('x')", "@myfile.lua")
print(pcall(g))
local h = L("error('x')")
print(pcall(h))
local long = L("error('x')", string.rep("y", 100))
print(pcall(long))
