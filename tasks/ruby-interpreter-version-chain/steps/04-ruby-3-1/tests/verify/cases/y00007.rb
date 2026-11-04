begin
  p({a: 1, b: 2}.transform_values { |v| v * 10 })
rescue Exception => e
  p e.class
end
