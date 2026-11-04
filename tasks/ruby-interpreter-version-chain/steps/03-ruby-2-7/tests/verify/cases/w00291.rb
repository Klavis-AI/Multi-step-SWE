begin
  {}.fetch(:k)
  p :no_error
rescue Exception => e
  p e.class
  p e.message
end
