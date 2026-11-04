begin
  p({a: 1, b: 2}.filter_map { |x| x })
rescue Exception => e
  p e.class
end
