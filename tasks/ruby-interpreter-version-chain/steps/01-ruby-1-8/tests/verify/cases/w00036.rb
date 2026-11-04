begin
  p(1) if 1
rescue Exception => __e
  p __e.class
end
