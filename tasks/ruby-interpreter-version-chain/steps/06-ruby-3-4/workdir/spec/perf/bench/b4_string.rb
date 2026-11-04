# string construction and inspection
out = 1
i = 0
while i < 20000
  s = "abc" + i.to_s
  out = (out * 17 + s.length + s[0].ord) % 1000003
  i += 1
end
p out
