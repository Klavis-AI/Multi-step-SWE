begin
  r = Ractor.new { 6 * 7 }
  p r.take
rescue Exception => e
  p e.class
end
