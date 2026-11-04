begin
  p({a: 1, b: 2}.filter_map { |k, v| k if v > 1 })
rescue Exception => e
  p e.class
end
