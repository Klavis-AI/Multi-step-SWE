begin
  p("a\tb".tr('a-c', 'x-z'))
rescue Exception => e
  p e.class
end
