begin
  s = "abc"
  s << "d"
  p s
rescue Exception => __e
  p __e.class
end
