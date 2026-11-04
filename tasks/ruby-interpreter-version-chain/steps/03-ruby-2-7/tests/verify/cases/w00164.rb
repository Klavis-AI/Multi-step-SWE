begin
  p("日本語".sub(/b/, 'X'))
rescue Exception => e
  p e.class
end
