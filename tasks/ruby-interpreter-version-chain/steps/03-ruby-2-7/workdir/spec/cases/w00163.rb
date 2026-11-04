begin
  p("日本語".scan(/./))
rescue Exception => e
  p e.class
end
