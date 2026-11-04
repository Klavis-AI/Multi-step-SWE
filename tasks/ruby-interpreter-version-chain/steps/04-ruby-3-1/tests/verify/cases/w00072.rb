begin
  x = 1
  y = x + 1
  p y
rescue Exception => __e
  p __e.class
end
