begin
  raise ArgumentError, 'x'
  p :no_error
rescue Exception => e
  p e.class
  p e.message
end
