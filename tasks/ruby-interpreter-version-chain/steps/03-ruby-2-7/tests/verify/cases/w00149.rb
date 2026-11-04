begin
  p("abc".codepoints)
rescue Exception => e
  p e.class
end
