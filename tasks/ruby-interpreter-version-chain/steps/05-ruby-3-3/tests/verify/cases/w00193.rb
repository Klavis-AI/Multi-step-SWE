begin
  p("a\tb".center(9, '-'))
rescue Exception => e
  p e.class
end
