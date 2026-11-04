begin
  s = "hello".dup
  s.bytesplice(0, 1, "J")
  p s
rescue Exception => e
  p e.class
end
