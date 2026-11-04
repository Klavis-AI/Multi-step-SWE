begin
  p([[1, 2], [3, 4]].group_by { |x| 1 })
rescue Exception => e
  p e.class
end
