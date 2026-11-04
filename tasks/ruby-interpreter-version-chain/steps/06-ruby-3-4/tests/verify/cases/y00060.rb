begin
  p [1, 2].map { it }
rescue Exception => e
  p e.class
end
