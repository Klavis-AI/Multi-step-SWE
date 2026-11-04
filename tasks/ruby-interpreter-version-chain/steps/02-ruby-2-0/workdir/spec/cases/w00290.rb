begin
  [].fetch(3)
  p :no_error
rescue Exception => e
  p e.class
  p e.message
end
