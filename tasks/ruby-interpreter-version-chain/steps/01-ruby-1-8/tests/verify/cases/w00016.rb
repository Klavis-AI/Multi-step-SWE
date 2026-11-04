begin
  x = 1
  p [2].map { |x| x }
rescue Exception => __e
  p __e.class
end
