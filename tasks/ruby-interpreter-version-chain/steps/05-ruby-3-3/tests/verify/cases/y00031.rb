begin
  s = Set[1, 2, 3]
  p s.include?(2)
  p s.to_a.sort
rescue Exception => e
  p e.class
end
