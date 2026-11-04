begin
  require "set"
  s = Set.new([1])
  s << 2
  s.add?(2)
  p s.size
  p s.to_a.sort
rescue Exception => e
  p e.class
end
