begin
  require "set"
  a = Set[1, 2, 3]
  b = Set[3, 4]
  p (a | b).to_a.sort
  p (a & b).to_a
  p (a - b).to_a.sort
  p a.subset?(Set[1, 2, 3, 4])
rescue Exception => e
  p e.class
end
