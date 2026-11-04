r = [1, 2, 3].map do |i|
  next 9 if i == 2
  i
end
p r
