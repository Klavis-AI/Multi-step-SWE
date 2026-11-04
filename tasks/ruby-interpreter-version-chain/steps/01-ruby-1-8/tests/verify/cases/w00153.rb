begin
  p("abc".split(//))
rescue Exception => e
  p e.class
end
