begin
  1 / 0
  p :no_error
rescue Exception => e
  p e.class
  p e.message
end
