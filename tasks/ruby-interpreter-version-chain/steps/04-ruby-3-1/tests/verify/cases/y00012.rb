begin
  p 3.pow(4)
  p 3.pow(4, 5)
rescue Exception => e
  p e.class
end
