begin
  s = "hello".dup
  p s.byteindex("l")
  p s.byterindex("l")
  s.bytesplice(0, 1, "H")
  p s
rescue Exception => e
  p e.class
end
