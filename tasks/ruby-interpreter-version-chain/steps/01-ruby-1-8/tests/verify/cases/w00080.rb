begin
  p("abc" =~ /(b)(c)/)
  p $1
rescue Exception => __e
  p __e.class
end
