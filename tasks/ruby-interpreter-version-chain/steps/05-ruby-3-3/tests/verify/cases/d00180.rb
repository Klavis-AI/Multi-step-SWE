begin
  p("a".untaint)
rescue Exception => e
  p e.class
end
