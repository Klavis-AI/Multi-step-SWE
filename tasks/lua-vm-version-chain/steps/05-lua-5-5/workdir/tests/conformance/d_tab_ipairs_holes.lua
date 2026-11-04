local t = {1, 2, nil, 4}
local n = 0
for i, v in ipairs(t) do n = i end
print(n)
local m = setmetatable({}, {__index = function(_, i) return i <= 3 and i * 10 or nil end})
local c = 0
for i, v in ipairs(m) do c = c + v end
print(c)
