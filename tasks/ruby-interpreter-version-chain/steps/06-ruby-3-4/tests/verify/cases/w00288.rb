begin
  nil.foo
  p :no_error
rescue Exception => e
  p e.class
  p e.message
end
