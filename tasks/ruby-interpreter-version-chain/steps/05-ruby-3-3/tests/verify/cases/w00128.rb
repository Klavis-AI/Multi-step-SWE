begin
  p((1..4).minmax)
rescue Exception => e
  p e.class
end
