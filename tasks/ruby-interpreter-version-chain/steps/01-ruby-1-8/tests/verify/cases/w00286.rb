begin
  raise TypeError.new('t')
  p :no_error
rescue Exception => e
  p e.class
  p e.message
end
