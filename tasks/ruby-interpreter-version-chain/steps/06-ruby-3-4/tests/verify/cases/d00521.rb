begin
  s = "abc"; s.upcase!; p s
rescue Exception => e
  p e.class
end
