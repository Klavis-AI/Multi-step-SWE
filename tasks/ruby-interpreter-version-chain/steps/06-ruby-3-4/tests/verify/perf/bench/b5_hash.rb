# Hash read / modify / write
h = {}
i = 0
while i < 40000
  k = i % 997
  h[k] = ((h[k] || 2) * 5 + i) % 999983
  i += 1
end
p h.size
p h[11]
