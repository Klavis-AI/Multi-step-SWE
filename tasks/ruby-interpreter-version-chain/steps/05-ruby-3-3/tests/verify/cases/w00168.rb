begin
  p("日本語".tr('a-c', 'x-z'))
rescue Exception => e
  p e.class
end
