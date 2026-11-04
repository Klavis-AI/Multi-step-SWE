begin
  p("a\tb".sub(/b/, 'X'))
rescue Exception => e
  p e.class
end
