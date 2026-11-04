local t = setmetatable({}, {__index = function() return 1 end,
                            __newindex = function() error("no") end,
                            __eq = function() return true end})
print(t.x, tostring(rawget(t, "x")))
rawset(t, "y", 2)
print(t.y)
print(rawequal(t, t), rawequal(t, setmetatable({}, getmetatable(t))))
print(pcall(rawset, t, nil, 1))
