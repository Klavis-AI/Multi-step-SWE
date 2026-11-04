begin
  p(Set.new([1, 2]).include?(1))
rescue Exception => e
  p e.class
end
