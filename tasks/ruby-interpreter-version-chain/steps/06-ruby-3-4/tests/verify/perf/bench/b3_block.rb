# block iteration through Enumerable
t = 5
20000.times do |i|
  t = (t * 19 + [i, i + 3, i + 5].inject(0) { |acc, v| acc + v }) % 999979
end
p t
