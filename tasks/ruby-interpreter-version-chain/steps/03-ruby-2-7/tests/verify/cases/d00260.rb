begin
  p("abc".match?(/b/))
rescue Exception => e
  p e.class
end
