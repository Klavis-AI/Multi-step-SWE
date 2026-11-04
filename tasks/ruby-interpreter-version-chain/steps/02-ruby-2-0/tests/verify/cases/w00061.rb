begin
  p [1, 2].map { it }
rescue Exception => __e
  p __e.class
end
