begin
  p("a\tb".split(//))
rescue Exception => e
  p e.class
end
