print(1 and 2, nil and 2, false and 2, 1 or 2, nil or 2, false or 2)
print(not nil, not false, not 0, not "")
local function side(v) return v end
print(side(false) or "default")
print(nil == false)
