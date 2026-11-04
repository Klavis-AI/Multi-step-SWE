begin
  p "a".taint
rescue Exception => __e
  p __e.class
end
