begin
  p [1, 2, 3, 4].filter_map { |x| x * 2 if x.odd? }
rescue Exception => e
  p e.class
end
