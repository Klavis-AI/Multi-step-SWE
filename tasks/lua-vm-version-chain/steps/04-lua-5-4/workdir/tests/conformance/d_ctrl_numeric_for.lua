local out = {}
for i = 1, 5 do out[#out + 1] = i end
for i = 5, 1, -1 do out[#out + 1] = i end
for i = 1, 2, 0.5 do out[#out + 1] = i end
for i = 1, 0 do out[#out + 1] = "never" end
print(table.concat(out, ","))
print(pcall(load("for i = 1, 10, 0 do end")))
print(pcall(function() for i = 1, "x" do end end))
