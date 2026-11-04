begin
  s = "abc"; s << "d"; p s
rescue Exception => e
  p e.class
end
