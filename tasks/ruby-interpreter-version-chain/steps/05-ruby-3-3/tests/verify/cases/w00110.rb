begin
  p([[1, 2], [3, 4]].flat_map { |x| x })
rescue Exception => e
  p e.class
end
