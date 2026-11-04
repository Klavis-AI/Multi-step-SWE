begin
  p("日本語".each_char { })
rescue Exception => e
  p e.class
end
