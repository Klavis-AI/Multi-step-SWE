local L = load
if _VERSION == "Lua 5.1" then L = loadstring end
local f = L("return a")
print(tostring(f()))
if setfenv then
  setfenv(f, {a = 7})
  print(f())
  print(type(getfenv(f)))
else
  print("no setfenv")
  print("no getfenv")
end
