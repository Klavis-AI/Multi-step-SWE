begin
  p("a\tb".chars)
rescue Exception => e
  p e.class
end
