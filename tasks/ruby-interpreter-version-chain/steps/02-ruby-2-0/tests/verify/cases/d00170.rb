begin
  p("abc".each { |x| })
rescue Exception => e
  p e.class
end
