begin
  p("abc".center(9, '-'))
rescue Exception => e
  p e.class
end
