local obj = {n = 5}
function obj:get() return self.n end
function obj.stat(x) return x * 2 end
print(obj:get(), obj.stat(3))
local C = {}
C.__index = C
function C.new(v) return setmetatable({v = v}, C) end
function C:double() return self.v * 2 end
print(C.new(21):double())
