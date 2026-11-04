begin
  p "hello.rb".delete_suffix(".rb")
  p "x".delete_suffix("y")
rescue Exception => e
  p e.class
end
