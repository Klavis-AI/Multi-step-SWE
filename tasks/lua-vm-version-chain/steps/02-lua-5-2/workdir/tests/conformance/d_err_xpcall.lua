print(xpcall(function() error("boom") end, function(m) return "H:" .. tostring(m) end))
print(xpcall(function() return "fine" end, print))
print(xpcall(function() error({}) end, function(m) return type(m) end))
print(xpcall(function() error("x") end, function() error("in handler") end))
