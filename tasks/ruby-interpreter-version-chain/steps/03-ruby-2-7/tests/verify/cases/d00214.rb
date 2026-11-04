begin
  p([1, 2, 3].filter_map { |x| x * 2 if x.odd? })
rescue Exception => e
  p e.class
end
