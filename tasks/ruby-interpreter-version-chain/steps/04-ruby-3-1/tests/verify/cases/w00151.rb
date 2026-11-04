begin
  p("abc".sub(/b/, 'X'))
rescue Exception => e
  p e.class
end
