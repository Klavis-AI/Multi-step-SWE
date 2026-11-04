begin
  s = "hello".dup
  begin
    s.bytesplice(1, 2, "EY")
    p s
  rescue Exception => e
    p e.class
  end
  
rescue Exception => e
  p e.class
end
