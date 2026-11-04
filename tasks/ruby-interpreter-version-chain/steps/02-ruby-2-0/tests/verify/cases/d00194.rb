begin
  p("a" =~ /a/)
rescue Exception => e
  p e.class
end
