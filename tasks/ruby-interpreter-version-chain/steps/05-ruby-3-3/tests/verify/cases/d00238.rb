begin
  p("abcd".byteindex("c"))
rescue Exception => e
  p e.class
end
