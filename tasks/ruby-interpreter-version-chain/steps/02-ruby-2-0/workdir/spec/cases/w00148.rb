begin
  p("abc".bytes)
rescue Exception => e
  p e.class
end
