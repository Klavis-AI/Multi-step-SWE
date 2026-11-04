begin
  p "abc".frozen?
rescue Exception => e
  p e.class
end
