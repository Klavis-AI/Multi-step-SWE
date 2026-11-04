begin
  require "set"
  p Set[1].include?(1)
  p Set[].empty?
rescue Exception => e
  p e.class
end
