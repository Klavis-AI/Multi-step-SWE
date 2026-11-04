begin
  p({a: 1} in [1, 2])
rescue Exception => e
  p e.class
end
