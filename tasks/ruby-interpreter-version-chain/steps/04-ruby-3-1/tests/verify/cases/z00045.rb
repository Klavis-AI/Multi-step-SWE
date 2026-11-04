begin
  p [5].map { it }
rescue Exception => e
  p e.class
end
