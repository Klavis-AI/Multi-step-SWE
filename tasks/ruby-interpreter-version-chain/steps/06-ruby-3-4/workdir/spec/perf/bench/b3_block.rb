# block iteration through Enumerable
t = 1
20000.times do |i|
  t = (t * 13 + [i, i + 1, i + 2].inject(0) { |acc, v| acc + v }) % 1000003
end
p t
