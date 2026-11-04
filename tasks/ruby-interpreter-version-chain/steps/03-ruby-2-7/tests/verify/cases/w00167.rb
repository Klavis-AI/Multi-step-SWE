begin
  p("日本語".center(9, '-'))
rescue Exception => e
  p e.class
end
