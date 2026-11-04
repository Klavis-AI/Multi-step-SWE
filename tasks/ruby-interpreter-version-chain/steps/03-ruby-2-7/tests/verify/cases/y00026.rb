begin
  p((..3).include?(2))
  p [1, 2, 3][..1]
rescue Exception => e
  p e.class
end
