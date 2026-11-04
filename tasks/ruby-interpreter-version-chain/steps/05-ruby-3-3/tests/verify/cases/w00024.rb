begin
  X = 1
  X = 2
  p X
rescue Exception => __e
  p __e.class
end
