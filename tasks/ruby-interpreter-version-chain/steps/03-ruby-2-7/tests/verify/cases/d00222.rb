begin
  p(5.clamp(1, 3))
rescue Exception => e
  p e.class
end
