local mt = {}
for _, ev in ipairs{"__add", "__sub", "__mul", "__div", "__mod", "__pow", "__unm"} do
  mt[ev] = function(a, b) return ev end
end
local t = setmetatable({}, mt)
print(t + 1, 1 + t, t - 1, t * 2, t / 2, t % 2, t ^ 2, -t)
print(pcall(function() return t .. "x" end))
