local t = {1, 2, 3}
table.insert(t, 4); print(table.concat(t, ","))
table.insert(t, 1, 0); print(table.concat(t, ","))
print(table.remove(t), table.concat(t, ","))
print(table.remove(t, 1), table.concat(t, ","))
print(pcall(table.insert, t, 99, 1))
print(pcall(table.insert, t))
local e = {}
print(tostring(table.remove(e)))
