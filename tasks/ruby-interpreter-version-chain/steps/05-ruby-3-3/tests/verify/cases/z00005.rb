begin
  p "hello".byterindex("l")
  p "hello".byteindex("l")
rescue Exception => e
  p e.class
end
