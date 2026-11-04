begin
  p("a\tb".scan(/./))
rescue Exception => e
  p e.class
end
