begin
  p [1, 2].intersect?([2, 3])
  p [1].intersect?([9])
rescue Exception => e
  p e.class
end
