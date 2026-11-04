begin
  p "a".taint
rescue Exception => e
  p e.class
end
begin
  p "a".untaint
rescue Exception => e
  p e.class
end
begin
  p Object.new.trust
rescue Exception => e
  p e.class
end
begin
  p "a".tainted?
rescue Exception => e
  p e.class
end
