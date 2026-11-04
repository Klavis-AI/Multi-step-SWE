begin
  p({a: 1, b: 2}.fetch_values(:a, :b))
rescue Exception => e
  p e.class
end
