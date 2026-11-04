local a = setmetatable({}, {__mode = "kv"})
local x = {}
a[1] = x
a[x] = 1
a["s"] = "s"
do local y = {} a[y] = y end
collectgarbage()
local n = 0
for k, v in pairs(a) do n = n + 1 end
print(n, a[1] == x, a[x])
