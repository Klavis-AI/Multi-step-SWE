begin
  p 5.clamp(1, 3)
  p 5.clamp(1..3)
  p "m".clamp("a", "f")
rescue Exception => e
  p e.class
end
