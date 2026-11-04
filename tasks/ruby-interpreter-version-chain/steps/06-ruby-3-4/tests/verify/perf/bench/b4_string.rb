# string construction and inspection
out = 2
i = 0
while i < 20000
  s = "xyz" + i.to_s
  out = (out * 23 + s.length + s[0].ord) % 999961
  i += 1
end
p out
