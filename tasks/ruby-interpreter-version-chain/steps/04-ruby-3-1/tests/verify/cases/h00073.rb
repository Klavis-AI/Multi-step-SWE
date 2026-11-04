begin
  a = 1
  b = 2
  p({a:, b:})
rescue Exception => e
  p e.class
end
