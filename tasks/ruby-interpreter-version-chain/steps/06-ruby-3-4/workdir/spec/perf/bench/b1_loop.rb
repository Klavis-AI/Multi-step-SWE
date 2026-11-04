# integer arithmetic in a tight while loop; the running value is a non-linear
# recurrence, so there is no closed form to precompute
i = 0
s = 1
while i < 120000
  s = (s * 31 + i) % 1000003
  i += 1
end
p s
