begin
  p("abc".delete_prefix("a"))
rescue Exception => e
  p e.class
end
