begin
  p([[1, 2], [3, 4]].partition { |x| true })
rescue Exception => e
  p e.class
end
