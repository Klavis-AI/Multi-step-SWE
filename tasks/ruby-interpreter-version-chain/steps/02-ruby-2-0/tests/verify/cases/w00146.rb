begin
  p("abc".each_char { })
rescue Exception => e
  p e.class
end
