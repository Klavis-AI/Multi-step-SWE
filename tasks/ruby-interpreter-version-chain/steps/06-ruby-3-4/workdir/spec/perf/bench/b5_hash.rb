# Hash read / modify / write
h = {}
i = 0
while i < 40000
  k = i % 1000
  h[k] = ((h[k] || 1) * 3 + i) % 1000003
  i += 1
end
p h.size
p h[7]
