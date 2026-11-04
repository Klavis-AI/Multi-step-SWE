begin
  p [1, 2, 3].map { it * 2 }
  p [4].map { it }
rescue Exception => e
  p e.class
end
