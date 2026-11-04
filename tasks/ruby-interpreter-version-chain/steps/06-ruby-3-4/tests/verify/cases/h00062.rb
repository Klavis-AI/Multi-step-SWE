begin
  p [1, 2, 3].map { it * 2 }
rescue Exception => e
  p e.class
end
