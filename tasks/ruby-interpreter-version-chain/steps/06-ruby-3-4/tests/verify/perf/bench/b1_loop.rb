# integer arithmetic in a tight while loop; the running value is a non-linear
# recurrence, so there is no closed form to precompute
i = 0
s = 7
while i < 120000
  s = (s * 37 + i) % 999983
  i += 1
end
p s
