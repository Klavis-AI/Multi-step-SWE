local name = "dual_io_lines.txt"
local f = assert(io.open(name, "w"))
f:write("a\nb\nc\n")
f:close()
local out = {}
for l in io.lines(name) do out[#out + 1] = l end
print(table.concat(out, "|"))
local h = assert(io.open(name))
print(io.type(h))
h:close()
print(io.type(h), tostring(io.type(1)))
print(pcall(function() return h:read("*a") end))
os.remove(name)
