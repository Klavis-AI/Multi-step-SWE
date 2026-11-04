begin
  p("a\tb".codepoints)
rescue Exception => e
  p e.class
end
