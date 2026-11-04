begin
  p("a\tb".squeeze)
rescue Exception => e
  p e.class
end
