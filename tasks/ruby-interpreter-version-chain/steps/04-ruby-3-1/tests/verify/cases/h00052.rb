begin
  v = 1
  Ractor.new { v }
  p :no_error
rescue Exception => e
  p e.class
end
