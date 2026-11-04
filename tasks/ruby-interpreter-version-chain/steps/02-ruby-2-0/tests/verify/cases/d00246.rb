begin
  p(Ractor.new { 1 + 1 }.take)
rescue Exception => e
  p e.class
end
