begin
  p("a".taint)
rescue Exception => e
  p e.class
end
