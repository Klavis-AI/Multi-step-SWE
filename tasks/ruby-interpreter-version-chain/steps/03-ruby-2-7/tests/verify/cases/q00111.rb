begin
  p({a: 1}.fetch_values(:a, :b))
rescue Exception => e
  p e.class
end
