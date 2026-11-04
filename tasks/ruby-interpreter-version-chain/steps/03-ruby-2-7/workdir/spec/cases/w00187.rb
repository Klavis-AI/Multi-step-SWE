begin
  p("a\tb".bytes)
rescue Exception => e
  p e.class
end
