begin
  p("a\tb".each_char { })
rescue Exception => e
  p e.class
end
