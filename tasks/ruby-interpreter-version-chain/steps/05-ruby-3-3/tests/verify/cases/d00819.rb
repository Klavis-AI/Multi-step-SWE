begin
  s = Set.new([1, 2, 2, 3])
  p s.size
  p s.include?(2)
  p (s & Set.new([2, 3])).to_a.sort
  p (s | Set.new([9])).to_a.sort
  p s.subset?(Set.new([1, 2, 3, 4]))
rescue Exception => e
  p e.class
end
