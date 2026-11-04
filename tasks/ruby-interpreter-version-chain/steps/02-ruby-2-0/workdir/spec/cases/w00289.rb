begin
  Integer('z')
  p :no_error
rescue Exception => e
  p e.class
  p e.message
end
