begin
  p(:abc.name)
rescue Exception => e
  p e.class
end
