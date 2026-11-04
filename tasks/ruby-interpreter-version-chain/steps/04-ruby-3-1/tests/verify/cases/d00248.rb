begin
  p([1, 2].intersect?([2]))
rescue Exception => e
  p e.class
end
