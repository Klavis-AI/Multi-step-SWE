begin
  s = "abc"
  t = s.dup
  t << "d"
  p [s, t]
rescue Exception => e
  p e.class
end
