begin
  x = 1
  r = Ractor.new { x }
  p r.take
rescue Exception => e
  p e.class
end
