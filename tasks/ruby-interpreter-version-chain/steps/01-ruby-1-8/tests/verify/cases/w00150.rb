begin
  p("abc".scan(/./))
rescue Exception => e
  p e.class
end
