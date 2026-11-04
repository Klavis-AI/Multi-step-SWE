begin
  s = :abc
  p s.name
  p s.name.frozen?
rescue Exception => e
  p e.class
end
