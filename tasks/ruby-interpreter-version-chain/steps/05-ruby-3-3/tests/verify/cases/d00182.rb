begin
  p("a".tainted?)
rescue Exception => e
  p e.class
end
