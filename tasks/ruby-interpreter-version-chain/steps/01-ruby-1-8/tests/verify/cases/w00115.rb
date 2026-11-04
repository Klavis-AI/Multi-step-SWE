begin
  p([[1, 2], [3, 4]].minmax)
rescue Exception => e
  p e.class
end
